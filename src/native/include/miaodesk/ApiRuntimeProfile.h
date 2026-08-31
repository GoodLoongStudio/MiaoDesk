#pragma once

#include <windows.h>
#include <wincred.h>
#include <shlobj.h>

#include <algorithm>
#include <array>
#include <cwctype>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace miaodesk::api_runtime_profile {
namespace fs = std::filesystem;

struct RuntimeProfile {
    bool found{};
    bool configured{};
    bool explicitDefault{};
    bool keyHeaderSafe{true};
    std::wstring id;
    std::wstring name;
    std::wstring serviceType;
    std::wstring providerId{L"openai-compatible"};
    std::wstring baseUrl;
    std::wstring endpoint{L"/chat/completions"};
    std::wstring model;
    std::wstring apiKey;
    std::wstring error;
};

inline std::wstring Trim(std::wstring value) {
    const auto visible = [](wchar_t ch) { return !std::iswspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), visible));
    value.erase(std::find_if(value.rbegin(), value.rend(), visible).base(), value.end());
    return value;
}

inline std::wstring Lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return value;
}

inline bool ParseBool(const std::wstring& value) {
    return value == L"1" || _wcsicmp(value.c_str(), L"true") == 0 || _wcsicmp(value.c_str(), L"yes") == 0;
}

inline fs::path LocalStateRoot() {
    PWSTR raw = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &raw)) && raw) {
        fs::path root(raw);
        CoTaskMemFree(raw);
        return root / L"MiaoDesk";
    }
    wchar_t localAppData[32768]{};
    const DWORD count = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData,
                                                 static_cast<DWORD>(std::size(localAppData)));
    if (count > 0 && count < std::size(localAppData)) {
        return fs::path(std::wstring(localAppData, count)) / L"MiaoDesk";
    }
    return fs::temp_directory_path() / L"MiaoDesk";
}

inline fs::path ProfilesPath() { return LocalStateRoot() / L"api-profiles.ini"; }

inline std::wstring ReadIni(const fs::path& path, const wchar_t* section, const wchar_t* key,
                            const wchar_t* fallback = L"") {
    std::array<wchar_t, 4096> buffer{};
    GetPrivateProfileStringW(section, key, fallback, buffer.data(),
                             static_cast<DWORD>(buffer.size()), path.c_str());
    return buffer.data();
}

inline std::vector<std::wstring> ProfileSections() {
    const auto path = ProfilesPath();
    std::array<wchar_t, 32768> sections{};
    GetPrivateProfileSectionNamesW(sections.data(), static_cast<DWORD>(sections.size()), path.c_str());
    std::vector<std::wstring> result;
    for (const wchar_t* cursor = sections.data(); *cursor; cursor += std::wcslen(cursor) + 1) {
        if (wcsncmp(cursor, L"profile:", 8) == 0) result.emplace_back(cursor);
    }
    return result;
}

inline std::wstring ReadCredential(std::wstring_view target) {
    if (target.empty()) return {};
    PCREDENTIALW credential = nullptr;
    const std::wstring stableTarget(target);
    if (!CredReadW(stableTarget.c_str(), CRED_TYPE_GENERIC, 0, &credential)) return {};
    std::wstring value;
    if (credential && credential->CredentialBlob && credential->CredentialBlobSize > 0 &&
        (credential->CredentialBlobSize % sizeof(wchar_t)) == 0) {
        const auto* chars = reinterpret_cast<const wchar_t*>(credential->CredentialBlob);
        value.assign(chars, credential->CredentialBlobSize / sizeof(wchar_t));
    }
    if (credential) CredFree(credential);
    return Trim(std::move(value));
}

// Header values may contain ordinary ASCII spaces. The previous runtime guard rejected U+0020,
// which was stricter than HTTP and incorrectly treated working legacy credentials as corrupt.
// Reject controls/non-ASCII only; this still catches the original U+8BBE ByteString failure.
inline bool IsHttpHeaderSafe(std::wstring_view value, std::size_t* invalidIndex = nullptr,
                             unsigned* invalidCodepoint = nullptr) {
    for (std::size_t i = 0; i < value.size(); ++i) {
        const unsigned ch = static_cast<unsigned>(value[i]);
        if (ch < 0x20u || ch > 0x7eu) {
            if (invalidIndex) *invalidIndex = i;
            if (invalidCodepoint) *invalidCodepoint = ch;
            return false;
        }
    }
    return true;
}

inline bool NeedsKey(const std::wstring& baseUrl) {
    const auto lower = Lower(baseUrl);
    return lower.find(L"127.0.0.1") == std::wstring::npos &&
           lower.find(L"localhost") == std::wstring::npos &&
           lower.find(L"[::1]") == std::wstring::npos;
}

inline RuntimeProfile ReadSection(const std::wstring& section) {
    RuntimeProfile profile;
    if (!section.starts_with(L"profile:")) return profile;
    const auto path = ProfilesPath();
    profile.found = true;
    profile.id = section.substr(8);
    profile.name = ReadIni(path, section.c_str(), L"name", L"API 配置");
    profile.serviceType = ReadIni(path, section.c_str(), L"type", L"OpenAI Compatible");
    profile.baseUrl = Trim(ReadIni(path, section.c_str(), L"baseUrl"));
    profile.model = Trim(ReadIni(path, section.c_str(), L"model"));
    profile.explicitDefault = ParseBool(ReadIni(path, section.c_str(), L"default", L"0"));
    profile.apiKey = ReadCredential(L"MiaoDesk/ApiProfile/" + profile.id);

    while (profile.baseUrl.size() > 1 && profile.baseUrl.back() == L'/') profile.baseUrl.pop_back();
    const auto lowerBase = Lower(profile.baseUrl);
    const auto lowerType = Lower(profile.serviceType);

    if (lowerBase.find(L"deepseek") != std::wstring::npos) {
        profile.providerId = L"deepseek";
    } else if (lowerType.find(L"anthropic") != std::wstring::npos ||
               lowerBase.find(L"anthropic") != std::wstring::npos) {
        profile.providerId = L"anthropic";
        profile.endpoint = L"/messages";
    } else if (lowerType.find(L"google") != std::wstring::npos ||
               lowerType.find(L"gemini") != std::wstring::npos ||
               lowerBase.find(L"generativelanguage") != std::wstring::npos) {
        profile.providerId = L"google";
    } else if (!NeedsKey(profile.baseUrl)) {
        profile.providerId = L"local-openai-compatible";
    }

    const auto normalized = Lower(profile.baseUrl);
    for (const wchar_t* suffix : {L"/chat/completions", L"/responses", L"/messages"}) {
        const std::wstring value(suffix);
        if (normalized.size() >= value.size() && normalized.ends_with(value)) {
            profile.endpoint = value;
            profile.baseUrl.resize(profile.baseUrl.size() - value.size());
            while (profile.baseUrl.size() > 1 && profile.baseUrl.back() == L'/') profile.baseUrl.pop_back();
            break;
        }
    }

    std::size_t invalidIndex = 0;
    unsigned invalidCodepoint = 0;
    profile.keyHeaderSafe = profile.apiKey.empty() ||
        IsHttpHeaderSafe(profile.apiKey, &invalidIndex, &invalidCodepoint);
    if (!profile.keyHeaderSafe) {
        profile.error = L"默认 API Profile 的 API Key 含非法 HTTP Header 字符（位置 " +
                        std::to_wstring(invalidIndex) + L"，Unicode " +
                        std::to_wstring(invalidCodepoint) + L"）";
    }

    profile.configured = !profile.baseUrl.empty() && !profile.model.empty() &&
                         (!NeedsKey(profile.baseUrl) || !profile.apiKey.empty()) &&
                         profile.keyHeaderSafe;
    return profile;
}

inline RuntimeProfile LoadDefault() {
    const auto sections = ProfileSections();
    RuntimeProfile firstConfigured;
    RuntimeProfile firstFound;
    for (const auto& section : sections) {
        RuntimeProfile profile = ReadSection(section);
        if (!profile.found) continue;
        if (!firstFound.found) firstFound = profile;
        if (profile.configured && !firstConfigured.found) firstConfigured = profile;
        if (profile.explicitDefault) return profile;
    }

    // Resilience for older settings files that lost the default bit: consume the first fully
    // configured profile instead of inventing another active-config database. UI remains the
    // owner of which profile is default; this is only a read-only fallback.
    if (firstConfigured.found) return firstConfigured;
    return firstFound;
}

} // namespace miaodesk::api_runtime_profile
