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

    // The API Configuration Center owns provider/model/profile state. Runtime consumers refresh
    // directly from its default Profile. model-settings.json remains migration-only state for
    // machines that have not yet created api-profiles.ini; it is no longer the preferred source.
    void ReloadConfig() {
        const ModelConfig legacy = LoadConfig();
        ModelConfig refreshed = legacy;
        const auto profile = api_runtime_profile::LoadDefault();

        if (profile.found) {
            refreshed.providerId = profile.providerId;
            refreshed.baseUrl = profile.baseUrl;
            refreshed.model = profile.model;
            refreshed.endpoint = profile.endpoint;
        } else {
            // Migration compatibility for older installs. Legacy builds could persist Endpoint
            // as a complete URL; normalize that representation only while legacy state is in use.
            std::wstring absoluteEndpoint = refreshed.endpoint;
            if (absoluteEndpoint.starts_with(L"/https://") ||
                absoluteEndpoint.starts_with(L"/http://")) {
                absoluteEndpoint.erase(absoluteEndpoint.begin());
            }

            if (absoluteEndpoint.starts_with(L"https://") ||
                absoluteEndpoint.starts_with(L"http://")) {
                URL_COMPONENTS parts{};
                parts.dwStructSize = sizeof(parts);
                parts.dwSchemeLength = static_cast<DWORD>(-1);
                parts.dwHostNameLength = static_cast<DWORD>(-1);
                parts.dwUrlPathLength = static_cast<DWORD>(-1);
                parts.dwExtraInfoLength = static_cast<DWORD>(-1);

                if (WinHttpCrackUrl(absoluteEndpoint.c_str(), 0, 0, &parts) &&
                    parts.lpszHostName && parts.dwHostNameLength > 0) {
                    const bool secure = parts.nScheme == INTERNET_SCHEME_HTTPS;
                    std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
                    if (host.find(L':') != std::wstring::npos && !host.starts_with(L"[")) {
                        host = L"[" + host + L"]";
                    }

                    refreshed.baseUrl = secure ? L"https://" : L"http://";
                    refreshed.baseUrl += host;
                    const bool defaultPort =
                        (secure && parts.nPort == INTERNET_DEFAULT_HTTPS_PORT) ||
                        (!secure && parts.nPort == INTERNET_DEFAULT_HTTP_PORT);
                    if (!defaultPort && parts.nPort != 0) {
                        refreshed.baseUrl += L":" + std::to_wstring(parts.nPort);
                    }

                    refreshed.endpoint.clear();
                    if (parts.lpszUrlPath && parts.dwUrlPathLength > 0) {
                        refreshed.endpoint.assign(parts.lpszUrlPath, parts.dwUrlPathLength);
                    }
                    if (parts.lpszExtraInfo && parts.dwExtraInfoLength > 0) {
                        refreshed.endpoint.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
                    }
                    if (refreshed.endpoint.empty()) refreshed.endpoint = L"/";
                }
            }

            const bool migrated =
                refreshed.baseUrl != legacy.baseUrl || refreshed.endpoint != legacy.endpoint;
            if (migrated) SaveConfig(refreshed);
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