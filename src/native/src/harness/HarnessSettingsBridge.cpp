#include "miaodesk/HarnessSettingsBridge.h"
#include "miaodesk/ApiRuntimeProfile.h"

#include <windows.h>
#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace fs = std::filesystem;

namespace miaodesk {
namespace {

constexpr wchar_t kHarnessCredentialEnv[] = L"MIAODESK_API_KEY";

std::string WideToUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int count = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (count <= 0) return {};
    std::string out(static_cast<std::size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), count, nullptr, nullptr);
    return out;
}

fs::path MiaoDeskRoot() {
    wchar_t localAppData[32768]{};
    const DWORD count = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, static_cast<DWORD>(std::size(localAppData)));
    if (count == 0 || count >= std::size(localAppData)) return {};
    return fs::path(std::wstring(localAppData, count)) / L"MiaoDesk";
}

std::wstring JoinApiUrl(const std::wstring& baseUrl, const std::wstring& endpoint) {
    if (endpoint.starts_with(L"https://") || endpoint.starts_with(L"http://")) return endpoint;
    std::wstring base = baseUrl;
    while (base.size() > 1 && base.back() == L'/') base.pop_back();
    if (endpoint.empty() || endpoint == L"-") return base;
    std::wstring suffix = endpoint;
    if (suffix.front() != L'/') suffix.insert(suffix.begin(), L'/');
    return base + suffix;
}

bool EndsWithInsensitive(const std::wstring& value, const std::wstring& suffix) {
    if (suffix.size() > value.size()) return false;
    const auto offset = value.size() - suffix.size();
    for (std::size_t i = 0; i < suffix.size(); ++i) {
        if (std::towlower(value[offset + i]) != std::towlower(suffix[i])) return false;
    }
    return true;
}

std::wstring HarnessBaseUrl(const std::wstring& baseUrl, const std::wstring& endpoint, bool anthropic) {
    std::wstring full = JoinApiUrl(baseUrl, endpoint);
    const std::wstring suffix = anthropic ? L"/messages" :
        (EndsWithInsensitive(endpoint, L"/responses") ? L"/responses" : L"/chat/completions");
    if (EndsWithInsensitive(full, suffix)) full.erase(full.size() - suffix.size());
    while (full.size() > 1 && full.back() == L'/') full.pop_back();
    return full;
}

std::string YamlQuote(const std::wstring& value) {
    const std::string utf8 = WideToUtf8(value);
    std::string out = "'";
    for (char ch : utf8) {
        if (ch == '\'') out += "''";
        else out.push_back(ch);
    }
    out.push_back('\'');
    return out;
}

std::string BuildSettingsYaml(const std::wstring& providerId,
                              const std::wstring& baseUrl,
                              const std::wstring& endpoint,
                              const std::wstring& model,
                              bool hasApiKey) {
    const bool anthropic = providerId == L"anthropic";
    const bool responses = EndsWithInsensitive(endpoint, L"/responses");
    const std::wstring protocol = anthropic ? L"anthropic-messages" :
                                  (responses ? L"openai-responses" : L"openai-completions");
    const std::wstring resolvedBase = HarnessBaseUrl(baseUrl, endpoint, anthropic);

    std::string yaml;
    yaml += "# Managed by MiaoDesk. Provider/Base URL/Model come from the API Configuration Center default profile.\n";
    yaml += "# API key stays in Windows Credential Manager and is injected only into the DSH child process.\n";
    yaml += "llm-pi-ai:\n";
    yaml += "  providers:\n";
    yaml += "    miaodesk:\n";
    yaml += "      displayName: 'MiaoDesk'\n";
    if (hasApiKey) yaml += "      apiKeyEnv: MIAODESK_API_KEY\n";
    yaml += "      api: " + YamlQuote(protocol) + "\n";
    yaml += "      baseURL: " + YamlQuote(resolvedBase) + "\n";
    yaml += "      models:\n";
    yaml += "        - id: " + YamlQuote(model) + "\n";
    yaml += "agent-default-model:\n";
    yaml += "  provider: miaodesk\n";
    yaml += "  model: " + YamlQuote(model) + "\n";
    return yaml;
}

std::string BuildUnconfiguredSettingsYaml() {
    return "# Managed by MiaoDesk. No complete API profile is configured yet.\n{}\n";
}

bool WriteAtomically(const fs::path& path, const std::string& content) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec) return false;

    fs::path temp = path;
    temp += L".tmp";
    {
        std::ofstream stream(temp, std::ios::binary | std::ios::trunc);
        if (!stream) return false;
        stream.write(content.data(), static_cast<std::streamsize>(content.size()));
        stream.flush();
        if (!stream) {
            stream.close();
            fs::remove(temp, ec);
            return false;
        }
    }

    if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        fs::remove(temp, ec);
        return false;
    }
    return true;
}

bool PrepareUnconfiguredSettings(const fs::path& dshHome, HarnessSettingsBridgeState* state) {
    if (!state) return false;
    if (!WriteAtomically(dshHome / L"settings.yaml", BuildUnconfiguredSettingsYaml())) {
        state->error = L"无法写入 MiaoDesk Harness 空 settings.yaml";
        return false;
    }
    state->configured = false;
    state->providerId.clear();
    state->model.clear();
    state->baseUrl.clear();
    state->protocol.clear();
    state->apiKey.clear();
    state->hasApiKey = false;
    state->error.clear();
    return true;
}

} // namespace

HarnessSettingsBridgeState PrepareHarnessSettingsBridge() {
    HarnessSettingsBridgeState state;
    const fs::path root = MiaoDeskRoot();
    if (root.empty()) {
        state.error = L"LOCALAPPDATA 不可用";
        return state;
    }

    const fs::path dshHome = root / L"Harness" / L"DshHome";
    state.dshHome = dshHome.wstring();
    std::error_code ec;
    fs::create_directories(dshHome, ec);
    if (ec) {
        state.error = L"无法创建 MiaoDesk Harness DSH_HOME";
        return state;
    }

    // DSH is a workbench/runtime and must be able to boot even before the user
    // has completed model configuration. Model configuration gates model calls,
    // not the local DSH web service itself. Keep a deterministic empty settings
    // file so stale/generated provider data cannot prevent the workbench from booting.
    const auto profile = api_runtime_profile::LoadDefault();
    if (!profile.found || !profile.configured) {
        PrepareUnconfiguredSettings(dshHome, &state);
        return state;
    }

    state.providerId = profile.providerId;
    state.model = profile.model;
    state.apiKey = profile.apiKey;
    if (state.providerId.empty() || state.providerId == L"unconfigured") state.providerId = L"openai-compatible";

    const bool anthropic = state.providerId == L"anthropic";
    const bool responses = EndsWithInsensitive(profile.endpoint, L"/responses");
    state.protocol = anthropic ? L"anthropic-messages" :
                     (responses ? L"openai-responses" : L"openai-completions");
    state.baseUrl = HarnessBaseUrl(profile.baseUrl, profile.endpoint, anthropic);
    if (state.baseUrl.empty()) {
        PrepareUnconfiguredSettings(dshHome, &state);
        return state;
    }

    state.hasApiKey = !state.apiKey.empty();
    const std::string yaml = BuildSettingsYaml(state.providerId, profile.baseUrl, profile.endpoint,
                                               state.model, state.hasApiKey);
    if (!WriteAtomically(dshHome / L"settings.yaml", yaml)) {
        state.error = L"无法写入 MiaoDesk Harness settings.yaml";
        state.apiKey.clear();
        return state;
    }

    state.configured = true;
    state.error.clear();
    return state;
}

bool HarnessSettingsBridgeSelfTest() {
    const std::string openAi = BuildSettingsYaml(L"openai-compatible",
                                                  L"https://gateway.example/v1",
                                                  L"/chat/completions",
                                                  L"demo/model",
                                                  true);
    const std::string anthropic = BuildSettingsYaml(L"anthropic",
                                                     L"https://api.anthropic.com/v1",
                                                     L"/messages",
                                                     L"claude-test",
                                                     true);
    const std::string responses = BuildSettingsYaml(L"openai-compatible",
                                                     L"https://gateway.example/v1",
                                                     L"/responses",
                                                     L"demo/responses",
                                                     true);
    const std::string unconfigured = BuildUnconfiguredSettingsYaml();
    return openAi.find("provider: miaodesk") != std::string::npos &&
           openAi.find("model: 'demo/model'") != std::string::npos &&
           openAi.find("baseURL: 'https://gateway.example/v1'") != std::string::npos &&
           openAi.find("apiKeyEnv: MIAODESK_API_KEY") != std::string::npos &&
           openAi.find("sk-") == std::string::npos &&
           anthropic.find("api: 'anthropic-messages'") != std::string::npos &&
           anthropic.find("baseURL: 'https://api.anthropic.com/v1'") != std::string::npos &&
           responses.find("api: 'openai-responses'") != std::string::npos &&
           responses.find("baseURL: 'https://gateway.example/v1'") != std::string::npos &&
           unconfigured.find("{}") != std::string::npos &&
           std::wstring(kHarnessCredentialEnv) == L"MIAODESK_API_KEY";
}

} // namespace miaodesk
