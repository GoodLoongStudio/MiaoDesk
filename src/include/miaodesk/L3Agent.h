#pragma once
#include "miaodesk/ApiRuntimeProfile.h"
#include <atomic>
#include <cstddef>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <windows.h>
#include <winhttp.h>

namespace miaodesk {

struct ModelConfig {
    std::wstring providerId{L"unconfigured"};
    std::wstring baseUrl;
    std::wstring model;
    std::wstring endpoint;
};

struct ModelProbeResult {
    bool ok{};
    DWORD statusCode{};
    std::wstring providerId;
    std::wstring protocolLabel;
    std::wstring baseUrl;
    std::wstring endpoint;
    std::wstring apiUrl;
    std::vector<std::wstring> models;
    std::wstring recommendedModel;
    std::wstring message;
};

class L3Agent {
public:
    using DeltaCallback = std::function<void(std::wstring)>;
    using DoneCallback = std::function<void(std::wstring)>;

    L3Agent();
    ~L3Agent();

    bool TryHandleLocal(const std::wstring& input, std::wstring& reply, bool& consumedSecret);
    void AskAsync(std::wstring prompt, DeltaCallback onDelta, DoneCallback onDone);
    void Stop();
    bool Busy() const noexcept { return busy_.load(); }

    // The API Configuration Center is the runtime source of truth. Pi, Direct Model and Harness
    // all resolve the same default Profile. If no Profile exists, runtime is explicitly
    // unconfigured rather than falling back to the old model-settings.json shadow database.
    void ReloadConfig() {
        ModelConfig refreshed;
        const auto profile = api_runtime_profile::LoadDefault();
        if (profile.found) {
            refreshed.providerId = profile.providerId;
            refreshed.baseUrl = profile.baseUrl;
            refreshed.model = profile.model;
            refreshed.endpoint = profile.endpoint;
            if (profile.configured) api_runtime_profile::RetireLegacyShadowState();
        }

        if (refreshed.providerId == config_.providerId &&
            refreshed.baseUrl == config_.baseUrl &&
            refreshed.model == config_.model &&
            refreshed.endpoint == config_.endpoint) {
            return;
        }

        Stop();
        if (worker_.joinable()) worker_.join();
        config_ = refreshed;
        std::scoped_lock lock(conversationMutex_);
        conversation_.clear();
    }

    const ModelConfig& Config() const noexcept { return config_; }
    bool HasApiKey() const;
    bool HasStoredApiKey() const;
    std::wstring CurrentApiUrl() const;

    ModelProbeResult ProbeModels(const std::wstring& apiUrl,
                                 const std::wstring& apiKeyOverride = {},
                                 bool useStoredApiKey = true) const;
    bool ApplyModelConfig(const ModelProbeResult& probe,
                          const std::wstring& model,
                          const std::wstring& apiKeyOverride,
                          bool preserveExistingKey,
                          std::wstring& reply);

    std::size_t ConversationTurnCountForSelfTest() {
        std::scoped_lock lock(conversationMutex_);
        return conversation_.size();
    }

private:
    struct ChatTurn {
        std::wstring user;
        std::wstring assistant;
    };

    // Legacy persistence helpers remain private only for compatibility with old local commands;
    // ReloadConfig and the normal runtime path do not consume their state.
    ModelConfig LoadConfig() const;
    bool SaveConfig(const ModelConfig& config) const;
    std::wstring LoadApiKey() const;
    bool SaveApiKey(const std::wstring& key) const;
    void RunRequest(std::wstring prompt, DeltaCallback onDelta, DoneCallback onDone, std::stop_token stopToken);
    void ClearConversation();

    ModelConfig config_;
    std::jthread worker_;
    std::atomic_bool busy_{false};
    std::atomic<HINTERNET> activeRequest_{nullptr};
    std::mutex conversationMutex_;
    std::vector<ChatTurn> conversation_;
};

} // namespace miaodesk

#ifdef MIAODESK_L3_WINHTTP_TRACE
#include "miaodesk/ModelCredentialGuard.h"
#include "miaodesk/L3WinHttpTrace.h"
#endif
