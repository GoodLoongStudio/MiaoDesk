#pragma once

#include <windows.h>
#include <wincred.h>

#include "miaodesk/AppPaths.h"
#include "miaodesk/MiaoIniReadLimit.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cwchar>
#include <cwctype>
#include <filesystem>
#include <iterator>
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
    // Optional model capability hints. 0 means "not configured"; consumers apply
    // their own default. These must not be hardcoded per model, otherwise the
    // product reports a context window the selected model does not actually have.
    unsigned contextWindow{};
    unsigned maxTokens{};
    // Optional image endpoint. Empty imageProvider/imageBaseUrl inherit the chat
    // profile for backward compatibility. A distinct endpoint lets a local vLLM chat
    // service and an OpenAI-compatible image shim live on different ports.
    std::wstring imageProvider;
    std::wstring imageBaseUrl;
    std::wstring imageApiKey;
    // Empty means image generation is not configured.
    std::wstring imageModel;
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

// Optional positive integer for profile keys such as contextWindow / maxTokens.
// Returns 0 when the key is absent, empty, non-numeric or out of range so callers
// can treat 0 as "not configured" and apply their own default.
inline unsigned ParsePositiveUInt(const std::wstring& value) {
    if (value.empty()) return 0;
    wchar_t* end = nullptr;
    const unsigned long parsed = wcstoul(value.c_str(), &end, 10);
    if (end == value.c_str() || parsed == 0 || parsed > 4000000ul) return 0;
    while (end && *end && std::iswspace(*end)) ++end;
    if (end && *end != L'\0') return 0;
    return static_cast<unsigned>(parsed);
}

inline fs::path LocalStateRoot() {
    return miaodesk::paths::StateRoot();
}

// 最近一次读配置时是否撞上了缓冲区边界。用一个进程内的记录点,是因为
// "值被截断"这件事发生在最底层的 ReadIni/ProfileSections 里,而**该知道的人在最上层**
// (用户在 API 配置中心看这个 profile)。中间每一层都加一个出参会让签名全变,
// 而截断本来就是罕见事件 —— 记下来,让上层能问。
//
// 线程安全性:配置只在 UI 线程读写。真实情况里Pi/L3 的 worker 线程不碰 ini。
inline bool& LastReadTruncated() {
    static bool truncated = false;
    return truncated;
}

inline fs::path ProfilesPath() { return LocalStateRoot() / L"api-profiles.ini"; }

inline void RetireLegacyShadowState() {
    // Once a valid API Profile is authoritative, the old shadow database must not linger as a
    // second source of truth. Cleanup is best-effort and intentionally does not touch Profile
    // credentials under MiaoDesk/ApiProfile/<id>.
    std::error_code ec;
    fs::remove(LocalStateRoot() / L"model-settings.json", ec);
    CredDeleteW(L"MiaoDesk/ModelApiKey", CRED_TYPE_GENERIC, 0);
}

inline std::wstring ReadIni(const fs::path& path, const wchar_t* section, const wchar_t* key,
                            const wchar_t* fallback = L"") {
    std::array<wchar_t, 4096> buffer{};
    const DWORD copied = GetPrivateProfileStringW(section, key, fallback, buffer.data(),
                                                 static_cast<DWORD>(buffer.size()), path.c_str());
    // 这一行的返回值原来被丢掉了。Win32 在放不下时不报错 —— 它写一个截断的字符串,
    // 返回 nSize-2,而上层拿到一段看起来完全正常的文本。后果不是"少几个字符":
    // baseUrl 被截断时请求会打到另一台主机,而 **Key 也跟着去了**。
    // 判据在 MiaoIniReadLimit(纯逻辑,本机有门)。
    if (ini_read::ReadTruncated(static_cast<std::size_t>(copied), buffer.size())) {
        LastReadTruncated() = true;
    }
    return buffer.data();
}

inline std::vector<std::wstring> ProfileSections() {
    const auto path = ProfilesPath();
    std::array<wchar_t, 32768> sections{};
    const DWORD copied = GetPrivateProfileSectionNamesW(sections.data(), static_cast<DWORD>(sections.size()), path.c_str());
    // 同 ReadIni:段名清单一样会被静默截断。而这一侧的后果更直接 ——
    // 截断点之后的整个 profile 从列表里消失,用户在下拉里看不到它,
    // 而文件里它明明还在。P0-09 的"升级不丢 API profile"于是变成"丢了一整份且不报错"。
    if (ini_read::ReadTruncated(static_cast<std::size_t>(copied), sections.size())) {
        LastReadTruncated() = true;
    }
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
    profile.imageApiKey = ReadCredential(L"MiaoDesk/ApiProfile/" + profile.id + L"/Image");
    profile.contextWindow = ParsePositiveUInt(Trim(ReadIni(path, section.c_str(), L"contextWindow")));
    profile.maxTokens = ParsePositiveUInt(Trim(ReadIni(path, section.c_str(), L"maxTokens")));
    profile.imageProvider = Trim(ReadIni(path, section.c_str(), L"imageProvider"));
    profile.imageBaseUrl = Trim(ReadIni(path, section.c_str(), L"imageBaseUrl"));
    profile.imageModel = Trim(ReadIni(path, section.c_str(), L"imageModel"));

    while (profile.baseUrl.size() > 1 && profile.baseUrl.back() == L'/') profile.baseUrl.pop_back();
    while (profile.imageBaseUrl.size() > 1 && profile.imageBaseUrl.back() == L'/') profile.imageBaseUrl.pop_back();
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

    if (profile.imageProvider.empty()) {
        if (profile.imageBaseUrl.empty()) {
            profile.imageProvider = profile.providerId;
        } else {
            profile.imageProvider = NeedsKey(profile.imageBaseUrl)
                ? L"openai-compatible"
                : L"local-openai-compatible";
        }
    }
    if (profile.imageBaseUrl.empty()) profile.imageBaseUrl = profile.baseUrl;

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

inline std::vector<RuntimeProfile> LoadAll() {
    LastReadTruncated() = false;
    std::vector<RuntimeProfile> profiles;
    for (const auto& section : ProfileSections()) {
        RuntimeProfile profile = ReadSection(section);
        if (profile.found) profiles.push_back(std::move(profile));
    }
    return profiles;
}

// 最近一次读配置的过程中有没有东西被截断。给宿主/UI 用:它会说"这份配置可能不完整",
// 而**不会**改任何字段 —— 截断的那一份仍然按读到的内容走,只是不再安静。
inline bool LastConfigReadTruncated() noexcept { return LastReadTruncated(); }

inline RuntimeProfile LoadById(std::wstring_view id) {
    LastReadTruncated() = false;   // 每次读都重新判:上一轮的截断不该一直挂着
    if (id.empty()) return {};
    for (const auto& section : ProfileSections()) {
        if (!section.starts_with(L"profile:")) continue;
        if (_wcsicmp(section.c_str() + 8, std::wstring(id).c_str()) != 0) continue;
        return ReadSection(section);
    }
    return {};
}

inline RuntimeProfile LoadDefault() {
    // Product policy: the first *configured* profile in API Configuration Center is
    // the fallback used by a newly opened AI surface. Per-window selectors may choose
    // another profile without mutating this central list.
    //
    // Older builds persisted default=1. Keep reading that field in ReadSection for
    // backwards compatibility/migrations, but it no longer overrides list order.
    LastReadTruncated() = false;
    RuntimeProfile firstFound;
    for (const auto& section : ProfileSections()) {
        RuntimeProfile profile = ReadSection(section);
        if (!profile.found) continue;
        if (!firstFound.found) firstFound = profile;
        if (profile.configured) return profile;
    }
    return firstFound;
}

} // namespace miaodesk::api_runtime_profile
