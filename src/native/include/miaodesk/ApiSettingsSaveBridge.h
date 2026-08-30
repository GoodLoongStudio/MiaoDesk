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
#include <new>
#include <string>
#include <string_view>
#include <vector>

namespace miaodesk::api_settings_save_bridge {
namespace fs = std::filesystem;

constexpr wchar_t kPageClass[] = L"MiaoDesk.Native.ApiConfigurationCenter";
constexpr wchar_t kSubclassProperty[] = L"MiaoDesk.ApiSaveBridge.Installed";
constexpr wchar_t kStateProperty[] = L"MiaoDesk.ApiSaveBridge.State";
constexpr wchar_t kParentStateProperty[] = L"MiaoDesk.ApiSaveBridge.ParentState";
constexpr UINT_PTR kSubclassId = 0x4D415049;       // MAPI
constexpr UINT_PTR kParentSubclassId = 0x4D415053; // MAPS
constexpr UINT kRefreshScrollMessage = WM_APP + 0x35A;
constexpr UINT kCaptureSelectionMessage = WM_APP + 0x35B;
constexpr int kScrollbarId = 7399;
constexpr int kAiNavId = 6116;

constexpr int kProfileListId = 7300;
constexpr int kNameId = 7301;
constexpr int kTypeId = 7302;
constexpr int kApiUrlId = 7303;
constexpr int kApiKeyId = 7304;
constexpr int kModelId = 7305;
constexpr int kTimeoutId = 7306;
constexpr int kTemperatureId = 7307;
constexpr int kDefaultId = 7308;
constexpr int kStreamingId = 7309;
constexpr int kToolsId = 7310;
constexpr int kRetriesId = 7311;
constexpr int kNewId = 7312;
constexpr int kTestId = 7313;
constexpr int kSaveId = 7314;
constexpr int kDeleteId = 7315;
constexpr int kRevealId = 7316;
constexpr int kCopyId = 7317;

constexpr wchar_t kStoredKeyMask[] = L"************************";
constexpr wchar_t kActiveCredentialTarget[] = L"MiaoDesk/ModelApiKey";

struct BridgeState {
    HWND panel{};
    HWND parent{};
    HWND scrollbar{};
    int scrollOffset{};
    int contentHeight{};
    int viewportLeft{};
    int viewportTop{};
    int viewportWidth{};
    int viewportHeight{};
    std::wstring selectedSection;
};

inline int Scale(HWND hwnd, int px) {
    const UINT dpi = hwnd ? GetDpiForWindow(hwnd) : USER_DEFAULT_SCREEN_DPI;
    return MulDiv(px, static_cast<int>(dpi ? dpi : USER_DEFAULT_SCREEN_DPI), USER_DEFAULT_SCREEN_DPI);
}

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

inline void EnsureUnicodeIni(const fs::path& path) {
    std::error_code ec;
    if (fs::exists(path, ec)) return;
    if (!path.parent_path().empty()) fs::create_directories(path.parent_path(), ec);
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (!file || file == INVALID_HANDLE_VALUE) return;
    const BYTE bom[2]{0xff, 0xfe};
    DWORD written = 0;
    WriteFile(file, bom, sizeof(bom), &written, nullptr);
    CloseHandle(file);
}

inline std::wstring ReadIni(const fs::path& path, const wchar_t* section, const wchar_t* key,
                            const wchar_t* fallback = L"") {
    std::array<wchar_t, 4096> buffer{};
    GetPrivateProfileStringW(section, key, fallback, buffer.data(),
                             static_cast<DWORD>(buffer.size()), path.c_str());
    return buffer.data();
}

inline bool WriteIni(const fs::path& path, const wchar_t* section, const wchar_t* key,
                     const std::wstring& value) {
    EnsureUnicodeIni(path);
    return WritePrivateProfileStringW(section, key, value.c_str(), path.c_str()) != FALSE;
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

inline std::wstring ComboText(HWND combo) {
    if (!combo) return {};
    const int index = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
    if (index == CB_ERR) return {};
    wchar_t buffer[256]{};
    SendMessageW(combo, CB_GETLBTEXT, index, reinterpret_cast<LPARAM>(buffer));
    return buffer;
}

inline bool Checked(HWND button) {
    return button && SendMessageW(button, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

inline bool NeedsKey(const std::wstring& baseUrl) {
    const auto lower = Lower(baseUrl);
    return lower.find(L"127.0.0.1") == std::wstring::npos &&
           lower.find(L"localhost") == std::wstring::npos;
}

inline std::string Utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int length = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                                           nullptr, 0, nullptr, nullptr);
    if (length <= 0) return {};
    std::string result(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                        result.data(), length, nullptr, nullptr);
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
    const std::wstring serviceType = ComboText(GetDlgItem(panel, kTypeId));

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
    if (base.find(L"deepseek") != std::wstring::npos) {
        config.provider = L"deepseek";
    } else if (lowerType.find(L"anthropic") != std::wstring::npos ||
               base.find(L"anthropic") != std::wstring::npos) {
        config.provider = L"anthropic";
        config.endpoint = L"/messages";
    } else if (lowerType.find(L"google") != std::wstring::npos ||
               base.find(L"generativelanguage") != std::wstring::npos) {
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
    if (!MoveFileExW(temp.c_str(), path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::error_code ec;
        fs::remove(temp, ec);
        return false;
    }
    return true;
}

inline void CaptureSelectedSection(BridgeState& state) {
    if (!state.panel || !IsWindow(state.panel)) return;
    const std::wstring resolved = ResolveProfileSection(state.panel);
    if (!resolved.empty()) state.selectedSection = resolved;
}

inline bool SaveProfileFromControls(BridgeState& state) {
    HWND panel = state.panel;
    if (!panel) return false;

    std::wstring section = state.selectedSection;
    if (section.empty()) section = ResolveProfileSection(panel);
    if (section.empty()) section = L"profile:custom-" + std::to_wstring(GetTickCount64());

    const auto path = ProfilesPath();
    const std::wstring existingDefault = ReadIni(path, section.c_str(), L"default", L"0");
    std::wstring name = Text(GetDlgItem(panel, kNameId));
    if (name.empty()) name = L"API 配置";
    const std::wstring serviceType = ComboText(GetDlgItem(panel, kTypeId));
    const std::wstring baseUrl = Text(GetDlgItem(panel, kApiUrlId));
    const std::wstring model = Text(GetDlgItem(panel, kModelId));
    const bool wantsDefault = Checked(GetDlgItem(panel, kDefaultId));

    const std::wstring credentialTarget = CredentialTargetForSection(section);
    std::wstring keyField = Text(GetDlgItem(panel, kApiKeyId));
    std::wstring key = ReadCredential(credentialTarget);
    if (!keyField.empty() && keyField != kStoredKeyMask) {
        key = keyField;
        if (!WriteCredential(credentialTarget, key)) {
            MessageBoxW(panel, L"API Key 无法写入 Windows Credential Manager，配置尚未保存。",
                        L"妙喵 API 配置", MB_OK | MB_ICONERROR);
            return false;
        }
    }

    const bool configured = !baseUrl.empty() && !model.empty() && (!NeedsKey(baseUrl) || !key.empty());

    bool ok = true;
    ok = WriteIni(path, section.c_str(), L"name", name) && ok;
    ok = WriteIni(path, section.c_str(), L"type", serviceType.empty() ? L"OpenAI Compatible" : serviceType) && ok;
    ok = WriteIni(path, section.c_str(), L"baseUrl", baseUrl) && ok;
    ok = WriteIni(path, section.c_str(), L"model", model) && ok;
    ok = WriteIni(path, section.c_str(), L"timeout", Text(GetDlgItem(panel, kTimeoutId)).empty()
                      ? ReadIni(path, section.c_str(), L"timeout", L"60")
                      : Text(GetDlgItem(panel, kTimeoutId))) && ok;
    ok = WriteIni(path, section.c_str(), L"temperatureTenths",
                  ReadIni(path, section.c_str(), L"temperatureTenths", L"7")) && ok;
    ok = WriteIni(path, section.c_str(), L"retries", Text(GetDlgItem(panel, kRetriesId)).empty()
                      ? ReadIni(path, section.c_str(), L"retries", L"2")
                      : Text(GetDlgItem(panel, kRetriesId))) && ok;
    ok = WriteIni(path, section.c_str(), L"streaming", Checked(GetDlgItem(panel, kStreamingId)) ? L"1" : L"0") && ok;
    ok = WriteIni(path, section.c_str(), L"tools", Checked(GetDlgItem(panel, kToolsId)) ? L"1" : L"0") && ok;
    ok = WriteIni(path, section.c_str(), L"builtIn", ReadIni(path, section.c_str(), L"builtIn", L"0")) && ok;

    if (!ok) {
        MessageBoxW(panel, L"配置文件写入失败，请检查本机用户目录权限。",
                    L"妙喵 API 配置", MB_OK | MB_ICONERROR);
        return false;
    }

    bool madeDefault = false;
    bool defaultApplyFailed = false;
    if (wantsDefault && configured) {
        const bool keyOk = WriteCredential(kActiveCredentialTarget, key);
        const bool configOk = keyOk && WriteActiveConfig(BuildActiveConfig(panel));
        if (configOk) {
            for (const auto& item : ProfileSections()) {
                WriteIni(path, item.c_str(), L"default", item == section ? L"1" : L"0");
            }
            madeDefault = true;
        } else {
            defaultApplyFailed = true;
            WriteIni(path, section.c_str(), L"default", existingDefault);
        }
    } else if (!wantsDefault) {
        WriteIni(path, section.c_str(), L"default", L"0");
    } else {
        WriteIni(path, section.c_str(), L"default", existingDefault);
    }

    WriteIni(path, section.c_str(), L"lastOk", L"0");
    std::wstring message;
    if (madeDefault) {
        message = L"配置已保存并设为默认服务 · 可点击“测试连接”验证网络";
    } else if (defaultApplyFailed) {
        message = L"配置已保存，但应用默认服务失败";
    } else if (!configured) {
        message = L"配置草稿已保存 · 填写 Base URL、API Key 和模型后可测试连接";
    } else {
        message = L"配置已保存 · 可点击“测试连接”验证网络";
    }
    WriteIni(path, section.c_str(), L"lastMessage", message);

    state.selectedSection = section;
    if (HWND parent = state.parent; parent && IsWindow(parent)) {
        PostMessageW(parent, WM_COMMAND, MAKEWPARAM(kAiNavId, BN_CLICKED), 0);
        PostMessageW(parent, kRefreshScrollMessage, 0, 0);
    }

    if (defaultApplyFailed) {
        MessageBoxW(panel,
                    L"配置本身已经保存成功，但没有设成当前默认模型。请检查 Base URL、模型名称和 API Key。",
                    L"妙喵 API 配置", MB_OK | MB_ICONWARNING);
    }
    return true;
}

inline void Place(HWND hwnd, int x, int y, int w, int h) {
    if (!hwnd) return;
    SetWindowPos(hwnd, nullptr, x, y, std::max(1, w), std::max(1, h),
                 SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOREDRAW);
}

inline void RelayoutExpandedPanel(BridgeState& state) {
    HWND panel = state.panel;
    if (!panel) return;
    RECT area{};
    GetClientRect(panel, &area);
    const int width = std::max(1, static_cast<int>(area.right));
    const int height = std::max(1, static_cast<int>(area.bottom));
    const int margin = std::max(Scale(panel, 16), width / 40);
    const int contentW = std::max(Scale(panel, 500), width - margin * 2);
    const int headerH = Scale(panel, 72);
    const int statsH = Scale(panel, 82);
    const int bodyTop = margin + headerH + statsH + Scale(panel, 22);
    const int bodyH = std::max(Scale(panel, 360), height - bodyTop - margin);
    const int columnGap = Scale(panel, 14);
    const int leftW = std::clamp(contentW * 39 / 100, Scale(panel, 280), Scale(panel, 430));
    const int rightX = margin + leftW + columnGap;
    const int rightW = std::max(Scale(panel, 320), contentW - leftW - columnGap);

    const int saveW = Scale(panel, 132);
    const int addW = Scale(panel, 142);
    const int headerGap = Scale(panel, 8);
    Place(GetDlgItem(panel, kSaveId), margin + contentW - saveW, margin + Scale(panel, 2),
          saveW, Scale(panel, 42));
    Place(GetDlgItem(panel, kNewId), margin + contentW - saveW - headerGap - addW,
          margin + Scale(panel, 2), addW, Scale(panel, 42));
    SetWindowTextW(GetDlgItem(panel, kSaveId), L"保存配置");

    Place(GetDlgItem(panel, kProfileListId), margin + Scale(panel, 14), bodyTop + Scale(panel, 46),
          leftW - Scale(panel, 28), bodyH - Scale(panel, 118));

    const int fieldX = rightX + Scale(panel, 154);
    const int fieldW = rightW - Scale(panel, 174);
    const int rowH = Scale(panel, 34);
    const int gap = Scale(panel, 10);
    int y = bodyTop + Scale(panel, 54);
    Place(GetDlgItem(panel, kNameId), fieldX, y, fieldW, rowH); y += rowH + gap;
    Place(GetDlgItem(panel, kTypeId), fieldX, y, fieldW, Scale(panel, 180)); y += rowH + gap;
    Place(GetDlgItem(panel, kApiUrlId), fieldX, y, fieldW, rowH); y += rowH + gap;
    Place(GetDlgItem(panel, kApiKeyId), fieldX, y, std::max(Scale(panel, 120), fieldW - Scale(panel, 78)), rowH);
    Place(GetDlgItem(panel, kRevealId), fieldX + fieldW - Scale(panel, 72), y, Scale(panel, 32), rowH);
    Place(GetDlgItem(panel, kCopyId), fieldX + fieldW - Scale(panel, 36), y, Scale(panel, 32), rowH);
    y += rowH + Scale(panel, 20);
    Place(GetDlgItem(panel, kModelId), fieldX, y, fieldW, rowH); y += rowH + gap;
    Place(GetDlgItem(panel, kTimeoutId), fieldX, y, std::max(Scale(panel, 100), fieldW / 2), rowH); y += rowH + gap;
    Place(GetDlgItem(panel, kTemperatureId), fieldX, y, std::max(Scale(panel, 100), fieldW / 2), rowH); y += rowH + gap;
    Place(GetDlgItem(panel, kDefaultId), fieldX, y, fieldW, rowH); y += rowH + gap;
    Place(GetDlgItem(panel, kStreamingId), fieldX, y, Scale(panel, 116), rowH);
    Place(GetDlgItem(panel, kToolsId), fieldX + Scale(panel, 122), y, Scale(panel, 116), rowH);
    Place(GetDlgItem(panel, kRetriesId), fieldX + Scale(panel, 244), y,
          std::max(Scale(panel, 70), fieldW - Scale(panel, 244)), rowH);

    const int actionY = bodyTop + bodyH - Scale(panel, 86);
    Place(GetDlgItem(panel, kTestId), rightX + Scale(panel, 18), actionY, Scale(panel, 126), Scale(panel, 40));
    Place(GetDlgItem(panel, kDeleteId), rightX + rightW - Scale(panel, 132), actionY,
          Scale(panel, 114), Scale(panel, 40));

    RedrawWindow(panel, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

inline void UpdatePanelClip(BridgeState& state) {
    if (!state.panel || state.viewportWidth <= 0 || state.viewportHeight <= 0) return;
    const int top = std::clamp(state.scrollOffset, 0, std::max(0, state.contentHeight - 1));
    const int bottom = std::min(state.contentHeight, top + state.viewportHeight);
    HRGN region = CreateRectRgn(0, top, state.viewportWidth, std::max(top + 1, bottom));
    if (!region) return;
    if (!SetWindowRgn(state.panel, region, TRUE)) DeleteObject(region);
}

inline void UpdateScrollbar(BridgeState& state) {
    if (!state.scrollbar || !IsWindow(state.scrollbar)) return;
    const int maxScroll = std::max(0, state.contentHeight - state.viewportHeight);
    SCROLLINFO info{};
    info.cbSize = sizeof(info);
    info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    info.nMin = 0;
    info.nMax = std::max(0, state.contentHeight - 1);
    info.nPage = static_cast<UINT>(std::max(1, state.viewportHeight));
    info.nPos = std::clamp(state.scrollOffset, 0, maxScroll);
    SetScrollInfo(state.scrollbar, SB_CTL, &info, TRUE);
    ShowWindow(state.scrollbar, maxScroll > 0 && IsWindowVisible(state.panel) ? SW_SHOW : SW_HIDE);
}

inline void ApplyScrollPosition(BridgeState& state, int requested) {
    if (!state.panel || !IsWindow(state.panel)) return;
    const int maxScroll = std::max(0, state.contentHeight - state.viewportHeight);
    state.scrollOffset = std::clamp(requested, 0, maxScroll);
    SetWindowPos(state.panel, HWND_TOP, state.viewportLeft,
                 state.viewportTop - state.scrollOffset, 0, 0,
                 SWP_NOSIZE | SWP_NOACTIVATE);
    UpdatePanelClip(state);
    UpdateScrollbar(state);
}

inline void RefreshScrollableLayout(BridgeState& state) {
    if (!state.parent || !state.panel || !IsWindow(state.parent) || !IsWindow(state.panel)) return;
    RECT parentClient{};
    GetClientRect(state.parent, &parentClient);
    state.viewportLeft = Scale(state.parent, 208);
    state.viewportTop = Scale(state.parent, 58);
    const int scrollW = std::max(Scale(state.parent, 13), Scale(state.parent, 15));
    state.viewportWidth = std::max(1, static_cast<int>(parentClient.right) - state.viewportLeft - scrollW);
    state.viewportHeight = std::max(1, static_cast<int>(parentClient.bottom) - state.viewportTop);
    state.contentHeight = std::max(state.viewportHeight, Scale(state.parent, 800));

    if (!state.scrollbar || !IsWindow(state.scrollbar)) {
        state.scrollbar = CreateWindowExW(0, L"SCROLLBAR", L"",
                                          WS_CHILD | SBS_VERT,
                                          0, 0, scrollW, state.viewportHeight,
                                          state.parent,
                                          reinterpret_cast<HMENU>(static_cast<INT_PTR>(kScrollbarId)),
                                          GetModuleHandleW(nullptr), nullptr);
    }
    if (state.scrollbar) {
        SetWindowPos(state.scrollbar, HWND_TOP,
                     static_cast<int>(parentClient.right) - scrollW, state.viewportTop,
                     scrollW, state.viewportHeight, SWP_NOACTIVATE);
    }

    const int maxScroll = std::max(0, state.contentHeight - state.viewportHeight);
    state.scrollOffset = std::clamp(state.scrollOffset, 0, maxScroll);
    SetWindowPos(state.panel, HWND_TOP,
                 state.viewportLeft, state.viewportTop - state.scrollOffset,
                 state.viewportWidth, state.contentHeight,
                 SWP_NOACTIVATE);
    UpdatePanelClip(state);
    RelayoutExpandedPanel(state);
    UpdateScrollbar(state);
}

inline LRESULT CALLBACK ParentSubclass(HWND parent, UINT message, WPARAM wParam, LPARAM lParam,
                                      UINT_PTR, DWORD_PTR refData) {
    auto* state = reinterpret_cast<BridgeState*>(refData);
    if (!state) return DefSubclassProc(parent, message, wParam, lParam);

    if (message == kRefreshScrollMessage) {
        RefreshScrollableLayout(*state);
        return 0;
    }

    if (message == WM_VSCROLL && reinterpret_cast<HWND>(lParam) == state->scrollbar) {
        int target = state->scrollOffset;
        const int line = Scale(parent, 44);
        switch (LOWORD(wParam)) {
        case SB_LINEUP: target -= line; break;
        case SB_LINEDOWN: target += line; break;
        case SB_PAGEUP: target -= std::max(line, state->viewportHeight - line); break;
        case SB_PAGEDOWN: target += std::max(line, state->viewportHeight - line); break;
        case SB_THUMBPOSITION:
        case SB_THUMBTRACK: {
            SCROLLINFO info{};
            info.cbSize = sizeof(info);
            info.fMask = SIF_TRACKPOS;
            GetScrollInfo(state->scrollbar, SB_CTL, &info);
            target = info.nTrackPos;
            break;
        }
        case SB_TOP: target = 0; break;
        case SB_BOTTOM: target = std::max(0, state->contentHeight - state->viewportHeight); break;
        default: return 0;
        }
        ApplyScrollPosition(*state, target);
        return 0;
    }

    if (message == WM_MOUSEWHEEL && IsWindowVisible(state->panel)) {
        const short delta = static_cast<short>(HIWORD(wParam));
        ApplyScrollPosition(*state, state->scrollOffset - MulDiv(delta, Scale(parent, 72), WHEEL_DELTA));
        return 0;
    }

    const LRESULT result = DefSubclassProc(parent, message, wParam, lParam);
    if (message == WM_SIZE || message == WM_DPICHANGED || message == WM_SHOWWINDOW) {
        PostMessageW(parent, kRefreshScrollMessage, 0, 0);
    }
    if (message == WM_NCDESTROY) {
        if (GetPropW(parent, kParentStateProperty) == reinterpret_cast<HANDLE>(state))
            RemovePropW(parent, kParentStateProperty);
        state->parent = nullptr;
        state->scrollbar = nullptr;
        RemoveWindowSubclass(parent, ParentSubclass, kParentSubclassId);
    }
    return result;
}

inline LRESULT CALLBACK PageSubclass(HWND panel, UINT message, WPARAM wParam, LPARAM lParam,
                                    UINT_PTR, DWORD_PTR refData) {
    auto* state = reinterpret_cast<BridgeState*>(refData);
    if (!state) return DefSubclassProc(panel, message, wParam, lParam);

    if (message == kCaptureSelectionMessage) {
        CaptureSelectedSection(*state);
        return 0;
    }

    if (message == WM_COMMAND) {
        const int id = LOWORD(wParam);
        const int code = HIWORD(wParam);
        if (id == kSaveId && code == BN_CLICKED) {
            SaveProfileFromControls(*state);
            return 0;
        }
        if (id == kProfileListId && code == LBN_SELCHANGE) {
            const LRESULT result = DefSubclassProc(panel, message, wParam, lParam);
            state->selectedSection.clear();
            CaptureSelectedSection(*state);
            return result;
        }
        if (id == kNewId && code == BN_CLICKED) {
            const LRESULT result = DefSubclassProc(panel, message, wParam, lParam);
            state->selectedSection.clear();
            return result;
        }
        if ((id == kTestId || id == kDeleteId) && code == BN_CLICKED) {
            const LRESULT result = DefSubclassProc(panel, message, wParam, lParam);
            state->selectedSection.clear();
            CaptureSelectedSection(*state);
            return result;
        }
    }

    if (message == WM_MOUSEWHEEL) {
        const short delta = static_cast<short>(HIWORD(wParam));
        ApplyScrollPosition(*state, state->scrollOffset - MulDiv(delta, Scale(panel, 72), WHEEL_DELTA));
        return 0;
    }

    if (message == WM_SHOWWINDOW) {
        const LRESULT result = DefSubclassProc(panel, message, wParam, lParam);
        if (wParam) {
            PostMessageW(panel, kCaptureSelectionMessage, 0, 0);
            if (state->parent) PostMessageW(state->parent, kRefreshScrollMessage, 0, 0);
        } else if (state->scrollbar) {
            ShowWindow(state->scrollbar, SW_HIDE);
        }
        return result;
    }

    if (message == WM_NCDESTROY) {
        const LRESULT result = DefSubclassProc(panel, message, wParam, lParam);
        if (state->parent && IsWindow(state->parent)) {
            if (GetPropW(state->parent, kParentStateProperty) == reinterpret_cast<HANDLE>(state))
                RemovePropW(state->parent, kParentStateProperty);
            RemoveWindowSubclass(state->parent, ParentSubclass, kParentSubclassId);
        }
        if (state->scrollbar && IsWindow(state->scrollbar)) DestroyWindow(state->scrollbar);
        RemovePropW(panel, kSubclassProperty);
        RemovePropW(panel, kStateProperty);
        RemoveWindowSubclass(panel, PageSubclass, kSubclassId);
        delete state;
        return result;
    }

    return DefSubclassProc(panel, message, wParam, lParam);
}

inline bool IsApiPage(HWND hwnd) {
    if (!hwnd) return false;
    wchar_t className[128]{};
    return GetClassNameW(hwnd, className, static_cast<int>(std::size(className))) &&
           wcscmp(className, kPageClass) == 0;
}

inline void InstallBridge(HWND panel) {
    if (!panel || GetPropW(panel, kSubclassProperty)) return;
    auto* state = new (std::nothrow) BridgeState{};
    if (!state) return;
    state->panel = panel;
    state->parent = GetParent(panel);

    if (!SetWindowSubclass(panel, PageSubclass, kSubclassId, reinterpret_cast<DWORD_PTR>(state))) {
        delete state;
        return;
    }
    SetPropW(panel, kSubclassProperty, reinterpret_cast<HANDLE>(1));
    SetPropW(panel, kStateProperty, reinterpret_cast<HANDLE>(state));

    if (state->parent && !GetPropW(state->parent, kParentStateProperty)) {
        if (SetWindowSubclass(state->parent, ParentSubclass, kParentSubclassId,
                              reinterpret_cast<DWORD_PTR>(state))) {
            SetPropW(state->parent, kParentStateProperty, reinterpret_cast<HANDLE>(state));
        }
    }

    PostMessageW(panel, kCaptureSelectionMessage, 0, 0);
    if (state->parent) PostMessageW(state->parent, kRefreshScrollMessage, 0, 0);
}

inline LRESULT CALLBACK HookProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code >= 0 && lParam) {
        const auto* message = reinterpret_cast<const CWPSTRUCT*>(lParam);
        if (message && IsApiPage(message->hwnd)) InstallBridge(message->hwnd);
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
