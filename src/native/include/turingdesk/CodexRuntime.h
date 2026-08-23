#pragma once
#include "turingdesk/L3Agent.h"
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <windows.h>

namespace turingdesk {

struct CodexRuntimeStatus {
    bool binaryAvailable{};
    bool providerCompatible{};
    bool running{};
    std::wstring binaryPath;
    std::wstring message;
};

// Codex is the primary L3 runtime. Native Responses providers connect directly;
// OpenAI-compatible Chat Completions providers are normalized through Codex Relay.
class CodexRuntime {
public:
    using DeltaCallback = std::function<void(std::wstring)>;
    using DoneCallback = std::function<void(std::wstring)>;

    CodexRuntime() = default;
    ~CodexRuntime();

    CodexRuntime(const CodexRuntime&) = delete;
    CodexRuntime& operator=(const CodexRuntime&) = delete;

    CodexRuntimeStatus Status(const L3Agent& agent) const;
    bool CanHandle(const L3Agent& agent) const;
    void AskAsync(const L3Agent& agent, std::wstring prompt, DeltaCallback onDelta, DoneCallback onDone);
    void Stop();
    void ResetSession();
    bool Busy() const noexcept { return busy_.load(); }

private:
    struct ProviderSetup {
        bool ok{};
        std::wstring baseUrl;
        std::wstring model;
        std::wstring apiKey;
        std::wstring signature;
        std::wstring message;
        bool relayRequired{};
        std::wstring upstreamBaseUrl;
        unsigned short relayPort{};
    };

    ProviderSetup BuildProviderSetup(const L3Agent& agent) const;
    std::wstring FindBinary(bool& isCliBinary) const;
    std::wstring FindRelayBinary() const;
    bool EnsureSession(const ProviderSetup& setup, std::wstring& error);
    bool LaunchRelay(const ProviderSetup& setup, std::wstring& error);
    bool WaitForRelayReady(const ProviderSetup& setup, DWORD timeoutMs, std::wstring& error) const;
    bool LaunchProcess(const ProviderSetup& setup, std::wstring& error);
    bool ConfigureCodexHome(const ProviderSetup& setup, std::wstring& codexHome, std::wstring& error) const;
    bool WriteLine(const std::string& line);
    bool ReadLine(std::string& line, DWORD timeoutMs, std::wstring& error);
    bool WaitForResponse(long long id, std::string& response, std::wstring& error, DWORD timeoutMs = 30000);
    void RunTurn(ProviderSetup setup, std::wstring prompt, DeltaCallback onDelta, DoneCallback onDone, std::stop_token stopToken);
    void CleanupProcess();

    std::jthread worker_;
    std::atomic_bool busy_{false};
    mutable std::mutex processMutex_;
    HANDLE process_{};
    HANDLE processThread_{};
    HANDLE relayProcess_{};
    HANDLE relayThread_{};
    HANDLE inputWrite_{};
    HANDLE outputRead_{};
    std::string readBuffer_;
    std::wstring threadId_;
    std::wstring sessionSignature_;
    long long nextRequestId_{1};
};

} // namespace turingdesk
