#pragma once

#include <windows.h>
#include <commctrl.h>
#include <wincred.h>
#include <shlobj.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace miaodesk::api_settings_save_bridge {
namespace fs = std::filesystem;

constexpr wchar_t kPageClass[] = L"MiaoDesk.Native.ApiConfigurationCenter";
constexpr wchar_t kSubclassProperty[] = L"MiaoDesk.ApiSaveBridge.Installed";
constexpr UINT_PTR kSubclassId = 0x4D415049; // MAPI
constexpr int kProfileListId = 7300;
constexpr int kNameId = 7301;
constexpr int kTypeId = 7302;
constexpr int kApiUrlId = 7303;
constexpr int kApiKeyId = 7304;
constexpr int kModelId = 7305;
constexpr int kDefaultId = 7308;
constexpr int kSaveId = 7314;
constexpr wchar_t kStoredKeyMask[] = L"************************";
constexpr wchar_t kActiveCredentialTarget[] = L"MiaoDesk/ModelApiKey";

inline std::wstring Text(HWND hwnd) {
    if (!hwnd) return {};
    const int length = GetWindowTextLengthW(hwnd);
    if (length <= 0) return {};
    std::wstring value(static_cast<std::size_t>(length) + 1, L'\0');
    GetWindowTextW(hwnd, value.data(), length + 1);
    value.resize(static_cast<std::size_t>(length));
    return value;
}

inline std::wstring Lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return value;
}

inline fs::path LocalStateRoot() {
    PWSTR raw = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &raw)) && raw) {
        fs::path root(raw);
        CoTaskMemFree(raw);
        std::error_code ec;
        root /= L"MiaoDesk";
        fs::create_directories(root, ec);
        return root;
    }
    return fs::temp_directory_path() / L"MiaoDesk";
}

inline fs::path ProfilesPath() { return LocalStateRoot() / L"api-profiles.ini"; }
inline fs::path ModelSettingsPath() { return LocalStateRoot() / L"model-settings.json"; }

inline std::wstring ReadIni(const fs::path& path, const wchar_t* section, const wchar_t* key) {
    std::array<wchar_t, 4096> buffer{};
    GetPrivateProfileStringW(section, key, L"", buffer.data(), static_cast<DWORD>(buffer.size()), path.c_str());
    return buffer.data();
}

inline std::vector<std::wstring> ProfileSections() {
    const auto path = ProfilesPath();
    std::array<wchar_t, 32768> sections{};
    GetPrivateProfileSectionNamesW(sections.data(), static_cast<DWORD>(sections.size()), path.c_str());
    std::vector<std::wstring> result;
    for (const wchar_t* cursor = sections.data(); *cursor; cursor += wcslen(cursor) + 1) {
        if (wcsncmp(cursor, L"profile:", 8) == 0) result.emplace_back(cursor);
    }
    return result;
}

inline std::wstring ResolveProfileSection(HWND panel) {
    const auto path = ProfilesPath();
    const std::wstring name = Text(GetDlgItem(panel, kNameId));
    const std::wstring base = Text(GetDlgItem(panel, kApiUrlId));
    const std::wstring model = Text(GetDlgItem(panel, kModelId));
    std::wstring fallback;
    for (const auto& section : ProfileSections()) {
        const bool sameName = ReadIni(path, section.c_str(), L"name") == name;
        const bool sameBase = ReadIni(path, section.c_str(), L"baseUrl") == base;
        const bool sameModel = ReadIni(path, section.c_str(), L"model") == model;
        if (sameName && sameBase && sameModel) return section;
        if (fallback.empty() && sameBase && sameModel) fallback = section;
    }
    return fallback;
}

inline std::wstring CredentialTargetForSection(const std::wstring& section) {
    if (!section.starts_with(L"profile:")) return {};
    return L"MiaoDesk/ApiProfile/" + section.substr(8);
}

inline std::wstring ReadCredential(std::wstring_view target) {
    if (target.empty()) return {};
    PCREDENTIALW credential = nullptr;
    if (!CredReadW(std::wstring(target).c_str(), CRED_TYPE_GENERIC, 0, &credential)) return {};
    std::wstring value;
    if (credential && credential->CredentialBlob && credential->CredentialBlobSize) {
        const auto* chars = reinterpret_cast<const wchar_t*>(credential->CredentialBlob);
        value.assign(chars, credential->CredentialBlobSize / sizeof(wchar_t));
    }
    if (credential) CredFree(credential);
    return value;
}

inline bool WriteCredential(std::wstring_view target, const std::wstring& value) {
    if (target.empty()) return false;
    if (value.empty()) {
        CredDeleteW(std::wstring(target).c_str(), CRED_TYPE_GENERIC, 0);
        return true;
    }
    std::wstring mutableTarget(target);
    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = mutableTarget.data();
    credential.CredentialBlobSize = static_cast<DWORD>(value.size() * sizeof(wchar_t));
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<wchar_t*>(value.data()));
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    credential.UserName = const_cast<wchar_t*>(L"MiaoDesk");
    return CredWriteW(&credential, 0) != FALSE;
}

inline std::string Utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int length = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0) return {};
    std::string result(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), length, nullptr, nullptr);
    return result;
}

inline std::string EscapeJson(const std::wstring& value) {
    const auto utf8 = Utf8(value);
    std::string result;
    result.reserve(utf8.size() + 16);
    for (unsigned char ch : utf8) {
        switch (ch) {
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:
            if (ch < 0x20) {
                char buffer[7]{};
                sprintf_s(buffer, "\\u%04x", static_cast<unsigned>(ch));
                result += buffer;
            } else {
                result.push_back(static_cast<char>(ch));
            }
        }
    }
    return result;
}

struct ActiveConfig {
    std::wstring provider{L"openai-compatible"};
    std::wstring baseUrl;
    std::wstring endpoint{L"/chat/completions"};
    std::wstring model;
};

inline ActiveConfig BuildActiveConfig(HWND panel) {
    ActiveConfig config;
    config.baseUrl = Text(GetDlgItem(panel, kApiUrlId));
    config.model = Text(GetDlgItem(panel, kModelId));
    std::wstring serviceType;
    if (HWND type = GetDlgItem(panel, kTypeId)) {
        const int index = static_cast<int>(SendMessageW(type, CB_GETCURSEL, 0, 0));
        if (index != CB_ERR) {
            wchar_t buffer[256]{};
            SendMessageW(type, CB_GETLBTEXT, index, reinterpret_cast<LPARAM>(buffer));
            serviceType = buffer;
        }
    }

    while (config.baseUrl.size() > 1 && config.baseUrl.back() == L'/') config.baseUrl.pop_back();
    const auto lowerBase = Lower(config.baseUrl);
    const auto lowerType = Lower(serviceType);
    for (const wchar_t* suffix : {L"/chat/completions", L"/responses", L"/messages"}) {
        const std::wstring value(suffix);
        if (lowerBase.size() >= value.size() && lowerBase.ends_with(value)) {
            config.endpoint = value;
            config.baseUrl.resize(config.baseUrl.size() - value.size());
            while (config.baseUrl.size() > 1 && config.baseUrl.back() == L'/') config.baseUrl.pop_back();
            break;
        }
    }

    const auto base = Lower(config.baseUrl);
    if (base.find(L"deepseek") != std::wstring::npos) config.provider = L"deepseek";
    else if (lowerType.find(L"anthropic") != std::wstring::npos || base.find(L"anthropic") != std::wstring::npos) {
        config.provider = L"anthropic";
        config.endpoint = L"/messages";
    } else if (lowerType.find(L"google") != std::wstring::npos || base.find(L"generativelanguage") != std::wstring::npos) {
        config.provider = L"google";
    }
    return config;
}

inline bool WriteActiveConfig(const ActiveConfig& config) {
    if (config.baseUrl.empty() || config.model.empty()) return false;
    const auto path = ModelSettingsPath();
    auto temp = path;
    temp += L".tmp";
    std::ofstream stream(temp, std::ios::binary | std::ios::trunc);
    if (!stream) return false;
    stream << "{\n"
           << "  \"ProviderId\": \"" << EscapeJson(config.provider) << "\",\n"
           << "  \"Mode\": \"direct\",\n"
           << "  \"BaseUrl\": \"" << EscapeJson(config.baseUrl) << "\",\n"
           << "  \"Model\": \"" << EscapeJson(config.model) << "\",\n"
           << "  \"Endpoint\": \"" << EscapeJson(config.endpoint) << "\"\n"
           << "}\n";
    stream.flush();
    const bool ok = static_cast<bool>(stream);
    stream.close();
    if (!ok) {
        std::error_code ec;
        fs::remove(temp, ec);
        return false;
    }
    if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::error_code ec;
        fs::remove(temp, ec);
        return false;
    }
    return true;
}

inline bool PromoteSavedProfileToDefault(HWND panel) {
    const auto section = ResolveProfileSection(panel);
    if (section.empty()) return false;
    const auto path = ProfilesPath();
    for (const auto& item : ProfileSections()) {
        WritePrivateProfileStringW(item.c_str(), L"default", item == section ? L"1" : L"0", path.c_str());
    }

    std::wstring key = Text(GetDlgItem(panel, kApiKeyId));
    if (key == kStoredKeyMask || key.empty()) key = ReadCredential(CredentialTargetForSection(section));
    if (!WriteCredential(kActiveCredentialTarget, key)) return false;
    return WriteActiveConfig(BuildActiveConfig(panel));
}

inline LRESULT CALLBACK PageSubclass(HWND panel, UINT message, WPARAM wParam, LPARAM lParam,
                                    UINT_PTR, DWORD_PTR) {
    if (message == WM_COMMAND && LOWORD(wParam) == kSaveId && HIWORD(wParam) == BN_CLICKED) {
        HWND defaultCheck = GetDlgItem(panel, kDefaultId);
        const bool wantsDefault = defaultCheck && SendMessageW(defaultCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
        if (!wantsDefault) return DefSubclassProc(panel, message, wParam, lParam);

        // The original page historically couples "save" with ProbeModels whenever the
        // profile is marked default. Temporarily save it as an ordinary profile first;
        // connection testing remains the responsibility of the dedicated Test button.
        SendMessageW(defaultCheck, BM_SETCHECK, BST_UNCHECKED, 0);
        const LRESULT result = DefSubclassProc(panel, message, wParam, lParam);
        SendMessageW(defaultCheck, BM_SETCHECK, BST_CHECKED, 0);

        if (PromoteSavedProfileToDefault(panel)) {
            // Re-enter the existing navigation path on the next message turn so the page
            // reloads its profile vector from disk and immediately shows the new default.
            if (HWND parent = GetParent(panel)) {
                PostMessageW(parent, WM_COMMAND, MAKEWPARAM(6116, BN_CLICKED), 0);
            }
        } else {
            MessageBoxW(panel, L"配置已写入，但设为当前默认模型时失败。请检查 Base URL、模型名称和 API Key。",
                        L"妙喵 API 配置", MB_OK | MB_ICONWARNING);
        }
        return result;
    }

    if (message == WM_NCDESTROY) {
        RemovePropW(panel, kSubclassProperty);
        RemoveWindowSubclass(panel, PageSubclass, kSubclassId);
    }
    return DefSubclassProc(panel, message, wParam, lParam);
}

inline bool IsApiPage(HWND hwnd) {
    if (!hwnd) return false;
    wchar_t className[128]{};
    return GetClassNameW(hwnd, className, static_cast<int>(std::size(className))) &&
           wcscmp(className, kPageClass) == 0;
}

inline LRESULT CALLBACK HookProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code >= 0 && lParam) {
        const auto* message = reinterpret_cast<const CWPSTRUCT*>(lParam);
        if (message && IsApiPage(message->hwnd) && !GetPropW(message->hwnd, kSubclassProperty)) {
            if (SetWindowSubclass(message->hwnd, PageSubclass, kSubclassId, 0)) {
                SetPropW(message->hwnd, kSubclassProperty, reinterpret_cast<HANDLE>(1));
            }
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

class Bridge final {
public:
    Bridge() : hook_(SetWindowsHookExW(WH_CALLWNDPROC, HookProc, nullptr, GetCurrentThreadId())) {}
    ~Bridge() { if (hook_) UnhookWindowsHookEx(hook_); }
    Bridge(const Bridge&) = delete;
    Bridge& operator=(const Bridge&) = delete;
private:
    HHOOK hook_{};
};

inline Bridge gBridge;

} // namespace miaodesk::api_settings_save_bridge
