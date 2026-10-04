#pragma once
#include "miaodesk/L3Agent.h"
#include "miaodesk/MiaoTurnLifecycle.h"
#include "miaodesk/PiLaunchProfile.h"
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <windows.h>

namespace miaodesk {

struct PiRuntimeStatus {
    bool nodeAvailable{};
    bool piAvailable{};
    bool running{};
    std::wstring nodePath;
    std::wstring piPath;
    std::wstring message;
};

enum class PiActivityKind {
    Understanding,
    ToolStarted,
    ToolFinished,
    Retrying,
    Recovered,
    Succeeded,
    Failed,
    Cancelled,
};

struct PiActivityEvent {
    PiActivityKind kind{PiActivityKind::Understanding};
    std::wstring toolName;
    std::wstring message;
    bool error{};
    std::wstring resultText;
};

class PiRuntime {
public:
    using DeltaCallback = std::function<void(std::wstring)>;
    using DoneCallback = std::function<void(std::wstring)>;
    using ActivityCallback = std::function<void(PiActivityEvent)>;

    PiRuntime() = default;
    ~PiRuntime();

    PiRuntime(const PiRuntime&) = delete;
    PiRuntime& operator=(const PiRuntime&) = delete;

    // Which launch configuration this runtime uses. Set before the first AskAsync;
    // changing it later restarts the process rather than mutating a live one.
    void SetLaunchProfile(PiLaunchProfile profile) { launchProfile_ = std::move(profile); }
    const PiLaunchProfile& LaunchProfile() const noexcept { return launchProfile_; }

    PiRuntimeStatus Status(const L3Agent& agent) const;
    bool CanHandle(const L3Agent& agent) const;
    void AskAsync(const L3Agent& agent, std::wstring prompt, DeltaCallback onDelta, DoneCallback onDone,
                  ActivityCallback onActivity = {});
    void SetActivityCallback(ActivityCallback callback);
    void Stop();
    void ResetSession();
    // 忙不忙看相位,不看一个孤立的 bool。"已请求取消、worker 还没退"也算忙 ——
    // AI-03 的缺陷就在这儿:Stop() 曾把那个 bool 直接清掉,于是取消之后能立刻起第二轮,
    // 与还没退完的第一轮共用同一个 Node 进程和同一根管子。见 MiaoTurnLifecycle.h。
    bool Busy() const noexcept { return turnPhase_.load() != turn_lifecycle::TurnPhase::Idle; }

    // 轮次是不是**真的在跑**。与 Busy() 只差 Stopping 那一态,而那一态正是两个问题
    // 的分界:"还能不能再起一轮"要把取消中算忙(AI-03),"该不该把结果拿给用户看"
    // 要把取消中算结束 —— 用户按了取消、界面已经写了"已停止。",那之后到达的预览
    // 不该再把窗口弹起来。两个都问 Busy() 就会有一个答错,所以分开问。
    bool TurnActive() const noexcept { return turnPhase_.load() == turn_lifecycle::TurnPhase::Running; }

private:
    struct ProviderSetup {
        bool ok{};
        std::wstring nodePath;
        std::wstring piPath;
        std::wstring agentDir;
        std::wstring providerId{L"miaodesk"};
        std::wstring apiType;
        std::wstring baseUrl;
        std::wstring model;
        std::wstring apiKey;
        std::wstring signature;
        std::wstring message;
        // Model capability hints. 0 means "not configured"; ConfigurePiAgent
        // applies its own default rather than reporting an invented value.
        unsigned contextWindow{};
        unsigned maxTokens{};
        // Image generation can use an endpoint independent from the chat provider.
        // This is required for a local vLLM chat port plus a separate OpenAI-compatible
        // image shim/ComfyUI port.
        std::wstring imageProvider;
        std::wstring imageBaseUrl;
        std::wstring imageApiKey;
        std::wstring imageModel;
        // From the launch profile. Kept in the setup because EnsureSession decides
        // whether to restart the process from the signature, and the profile is part
        // of that decision: two profiles must never share one child process.
        std::wstring sessionDir;
        std::wstring workingDirectory;
        std::wstring toolAllowlist;
        std::wstring systemPrompt;
    };

    ProviderSetup BuildProviderSetup(const L3Agent& agent) const;
    bool EnsureSession(const ProviderSetup& setup, std::wstring& error);
    bool LaunchProcess(const ProviderSetup& setup, std::wstring& error);
    bool ConfigurePiAgent(const ProviderSetup& setup, std::wstring& error) const;
    bool WriteLine(const std::string& line);
    bool ReadLine(std::string& line, DWORD timeoutMs, std::wstring& error);
    void RunTurn(ProviderSetup setup, std::wstring prompt, DeltaCallback onDelta, DoneCallback onDone,
                 ActivityCallback onActivity, std::stop_token stopToken);
    void CleanupProcess();

    std::jthread worker_;
    // 轮次相位取代了原来的 busy_ bool:两态分不出"取消中"与"真的闲"。
    // 由 UI 线程写(AskAsync/Stop)、worker 线程写(RunTurn 收尾),所以是 atomic。
    std::atomic<turn_lifecycle::TurnPhase> turnPhase_{turn_lifecycle::TurnPhase::Idle};
    mutable std::mutex processMutex_;
    mutable std::mutex callbackMutex_;
    ActivityCallback activityCallback_;
    HANDLE process_{};
    HANDLE processThread_{};
    HANDLE inputWrite_{};
    HANDLE outputRead_{};
    std::string readBuffer_;
    std::wstring sessionSignature_;
    PiLaunchProfile launchProfile_;
};

} // namespace miaodesk
