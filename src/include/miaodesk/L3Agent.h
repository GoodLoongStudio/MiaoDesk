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
    std::wstring profileId;
    std::wstring profileName;
    std::wstring providerId{L"unconfigured"};
    std::wstring baseUrl;
    std::wstring model;
    std::wstring endpoint;
    // Model capability hints from the active Profile. 0 means "not configured";
    // consumers apply their own default. Tracked here so a change forces the Pi
    // session to restart with the new values instead of silently keeping the old ones.
    unsigned contextWindow{};
    unsigned maxTokens{};
    // Image generation may use a separate OpenAI-compatible endpoint.
    std::wstring imageProvider;
    std::wstring imageBaseUrl;
    std::wstring imageApiKey;
    // Empty means image generation is not configured.
    std::wstring imageModel;
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

    // The API Configuration Center is the single source of truth. A window may
    // select one profile by id; an empty selection means "use the first configured
    // profile", which is the product-wide fallback for newly opened AI surfaces.
    void SetProfileId(std::wstring profileId) {
        profileId = api_runtime_profile::Trim(std::move(profileId));
        if (_wcsicmp(preferredProfileId_.c_str(), profileId.c_str()) == 0 &&
            (profileId.empty() || _wcsicmp(config_.profileId.c_str(), profileId.c_str()) == 0)) {
            return;
        }
        preferredProfileId_ = std::move(profileId);
        ReloadConfig();
    }

    void ReloadConfig() {
        ModelConfig refreshed;
        auto profile = preferredProfileId_.empty()
            ? api_runtime_profile::LoadDefault()
            : api_runtime_profile::LoadById(preferredProfileId_);
        if (!profile.found && !preferredProfileId_.empty()) {
            // A centrally managed profile may have been deleted while this window
            // stayed open. Fall back deterministically to the first configured one.
            preferredProfileId_.clear();
            profile = api_runtime_profile::LoadDefault();
        }
        if (profile.found) {
            refreshed.profileId = profile.id;
            refreshed.profileName = profile.name;
            refreshed.providerId = profile.providerId;
            refreshed.baseUrl = profile.baseUrl;
            refreshed.model = profile.model;
            refreshed.endpoint = profile.endpoint;
            refreshed.contextWindow = profile.contextWindow;
            refreshed.maxTokens = profile.maxTokens;
            refreshed.imageProvider = profile.imageProvider;
            refreshed.imageBaseUrl = profile.imageBaseUrl;
            refreshed.imageApiKey = profile.imageApiKey;
            refreshed.imageModel = profile.imageModel;
            if (profile.configured) api_runtime_profile::RetireLegacyShadowState();
        }

        if (refreshed.profileId == config_.profileId &&
            refreshed.profileName == config_.profileName &&
            refreshed.providerId == config_.providerId &&
            refreshed.baseUrl == config_.baseUrl &&
            refreshed.model == config_.model &&
            refreshed.endpoint == config_.endpoint &&
            refreshed.contextWindow == config_.contextWindow &&
            refreshed.maxTokens == config_.maxTokens &&
            refreshed.imageProvider == config_.imageProvider &&
            refreshed.imageBaseUrl == config_.imageBaseUrl &&
            refreshed.imageApiKey == config_.imageApiKey &&
            refreshed.imageModel == config_.imageModel) {
            return;
        }

        Stop();
        if (worker_.joinable()) worker_.join();
        config_ = refreshed;
        std::scoped_lock lock(conversationMutex_);
        conversation_.clear();
    }

    const ModelConfig& Config() const noexcept { return config_; }
    const std::wstring& ProfileId() const noexcept { return config_.profileId; }
    const std::wstring& ProfileName() const noexcept { return config_.profileName; }
    bool HasApiKey() const;
    bool HasStoredApiKey() const;
    std::wstring CurrentApiUrl() const;
    std::wstring CurrentApiKey() const { return LoadApiKey(); }

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
    std::wstring preferredProfileId_;
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
