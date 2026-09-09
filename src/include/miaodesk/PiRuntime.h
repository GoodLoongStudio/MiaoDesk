#pragma once
#include "miaodesk/L3Agent.h"
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

    PiRuntimeStatus Status(const L3Agent& agent) const;
    bool CanHandle(const L3Agent& agent) const;
    void AskAsync(const L3Agent& agent, std::wstring prompt, DeltaCallback onDelta, DoneCallback onDone,
                  ActivityCallback onActivity = {});
    void SetActivityCallback(ActivityCallback callback);
    void Stop();
    void ResetSession();
    bool Busy() const noexcept { return busy_.load(); }

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
    std::atomic_bool busy_{false};
    mutable std::mutex processMutex_;
    mutable std::mutex callbackMutex_;
    ActivityCallback activityCallback_;
    HANDLE process_{};
    HANDLE processThread_{};
    HANDLE inputWrite_{};
    HANDLE outputRead_{};
    std::string readBuffer_;
    std::wstring sessionSignature_;
};

} // namespace miaodesk
