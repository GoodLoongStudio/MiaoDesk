#include "miaodesk/DesktopAiSettingsPage.h"
#include "miaodesk/AppPaths.h"
#include "miaodesk/L3Agent.h"

#include <commctrl.h>
#include <shlobj.h>
#include <wincred.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace miaodesk::wallpaper {
namespace {

constexpr wchar_t kPageClass[] = L"MiaoDesk.Native.ApiConfigurationCenter";
constexpr wchar_t kStateProperty[] = L"MiaoDesk.ApiConfigurationCenter.State";
constexpr UINT_PTR kParentSubclassId = 0x54444150; // TDAP
constexpr int kFirstNavId = 6110;
constexpr int kAiNavId = 6116;

constexpr int kProfileListId = 7300;
constexpr int kNameId = 7301;
constexpr int kTypeId = 7302;
constexpr int kApiUrlId = 7303;
constexpr int kApiKeyId = 7304;
constexpr int kModelId = 7305;
constexpr int kNewId = 7312;
constexpr int kTestId = 7313;
constexpr int kSaveId = 7314;
constexpr int kDeleteId = 7315;
constexpr int kRevealId = 7316;
constexpr int kCopyId = 7317;
constexpr int kSetDefaultId = 7318;
constexpr int kProbeModelsId = 7319;
constexpr wchar_t kStoredKeyMask[] = L"************************";

HMENU ControlId(int id) {
    return reinterpret_cast<HMENU>(static_cast<INT_PTR>(id));
}

std::wstring WindowText(HWND window) {
    if (!window) return {};
    const int length = GetWindowTextLengthW(window);
    if (length <= 0) return {};
    std::wstring value(static_cast<std::size_t>(length) + 1, L'\0');
    GetWindowTextW(window, value.data(), length + 1);
    value.resize(static_cast<std::size_t>(length));
    return value;
}

fs::path LocalStateRoot() {
    return paths::EnsureStateRoot();
}

fs::path ProfilesPath() {
    return LocalStateRoot() / L"api-profiles.ini";
}

void EnsureUnicodeIni(const fs::path& path) {
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

std::wstring ReadIni(const fs::path& path, const wchar_t* section, const wchar_t* key,
                     const wchar_t* fallback = L"") {
    std::array<wchar_t, 4096> buffer{};
    GetPrivateProfileStringW(section, key, fallback, buffer.data(),
                             static_cast<DWORD>(buffer.size()), path.c_str());
    return buffer.data();
}

void WriteIni(const fs::path& path, const wchar_t* section, const wchar_t* key,
              const std::wstring& value) {
    EnsureUnicodeIni(path);
    WritePrivateProfileStringW(section, key, value.c_str(), path.c_str());
}

bool ParseBool(const std::wstring& value, bool fallback = false) {
    if (value.empty()) return fallback;
    return value == L"1" || _wcsicmp(value.c_str(), L"true") == 0 ||
           _wcsicmp(value.c_str(), L"yes") == 0;
}

std::wstring CredentialTarget(std::wstring_view id) {
    return L"MiaoDesk/ApiProfile/" + std::wstring(id);
}

std::wstring ReadCredential(std::wstring_view target) {
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

bool WriteCredential(std::wstring_view target, const std::wstring& value) {
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

void DeleteCredential(std::wstring_view target) {
    CredDeleteW(std::wstring(target).c_str(), CRED_TYPE_GENERIC, 0);
}

bool HeaderSafeSecret(const std::wstring& value) {
    return std::all_of(value.begin(), value.end(), [](wchar_t ch) {
        return ch >= 0x20 && ch <= 0x7e;
    });
}

struct ApiProfile {
    std::wstring id;
    std::wstring name;
    std::wstring serviceType{L"OpenAI Compatible"};
    std::wstring baseUrl;
    std::wstring model;
    bool isDefault{};
    bool lastOk{};
    std::wstring lastMessage;
    bool builtIn{};

    bool NeedsKey() const {
        return baseUrl.find(L"127.0.0.1") == std::wstring::npos &&
               baseUrl.find(L"localhost") == std::wstring::npos;
    }
};

class ApiProfileStore {
public:
    ApiProfileStore() : path_(ProfilesPath()) { EnsureUnicodeIni(path_); }

    std::vector<ApiProfile> Load() {
        std::vector<ApiProfile> profiles;
        std::array<wchar_t, 16384> sections{};
        GetPrivateProfileSectionNamesW(sections.data(), static_cast<DWORD>(sections.size()), path_.c_str());
        for (const wchar_t* cursor = sections.data(); *cursor; cursor += wcslen(cursor) + 1) {
            if (wcsncmp(cursor, L"profile:", 8) != 0) continue;
            ApiProfile profile;
            profile.id = cursor + 8;
            profile.name = ReadIni(path_, cursor, L"name", L"API 配置");
            profile.serviceType = ReadIni(path_, cursor, L"type", L"OpenAI Compatible");
            profile.baseUrl = ReadIni(path_, cursor, L"baseUrl");
            profile.model = ReadIni(path_, cursor, L"model");
            profile.isDefault = ParseBool(ReadIni(path_, cursor, L"default"));
            profile.lastOk = ParseBool(ReadIni(path_, cursor, L"lastOk"));
            profile.lastMessage = ReadIni(path_, cursor, L"lastMessage");
            profile.builtIn = ParseBool(ReadIni(path_, cursor, L"builtIn"));

            const bool hasKey = !ReadCredential(CredentialTarget(profile.id)).empty();
            const bool configured = !profile.baseUrl.empty() && !profile.model.empty() &&
                                    (!profile.NeedsKey() || hasKey);

            // Old builds seeded five placeholder profiles. Drop only empty placeholders;
            // if a user actually filled one, keep it and promote it to a normal profile.
            if (profile.builtIn && !configured) {
                WritePrivateProfileStringW(cursor, nullptr, nullptr, path_.c_str());
                DeleteCredential(CredentialTarget(profile.id));
                continue;
            }
            if (profile.builtIn) {
                profile.builtIn = false;
                WriteIni(path_, cursor, L"builtIn", L"0");
            }
            profiles.push_back(std::move(profile));
        }
        return profiles;
    }

    void Save(const ApiProfile& profile) {
        const std::wstring section = L"profile:" + profile.id;
        WriteIni(path_, section.c_str(), L"name", profile.name);
        WriteIni(path_, section.c_str(), L"type", profile.serviceType);
        WriteIni(path_, section.c_str(), L"baseUrl", profile.baseUrl);
        WriteIni(path_, section.c_str(), L"model", profile.model);
        WriteIni(path_, section.c_str(), L"default", profile.isDefault ? L"1" : L"0");
        WriteIni(path_, section.c_str(), L"lastOk", profile.lastOk ? L"1" : L"0");
        WriteIni(path_, section.c_str(), L"lastMessage", profile.lastMessage);
        WriteIni(path_, section.c_str(), L"builtIn", L"0");
    }

    void SaveAll(const std::vector<ApiProfile>& profiles) {
        for (const auto& profile : profiles) Save(profile);
    }

    void Remove(const ApiProfile& profile) {
        const std::wstring section = L"profile:" + profile.id;
        WritePrivateProfileStringW(section.c_str(), nullptr, nullptr, path_.c_str());
        DeleteCredential(CredentialTarget(profile.id));
    }

    bool HasKey(const ApiProfile& profile) const {
        return !ReadCredential(CredentialTarget(profile.id)).empty();
    }

    std::wstring Key(const ApiProfile& profile) const {
        return ReadCredential(CredentialTarget(profile.id));
    }

    bool SaveKey(const ApiProfile& profile, const std::wstring& key) const {
        return WriteCredential(CredentialTarget(profile.id), key);
    }

private:
    fs::path path_;
};

void FillSolid(HDC dc, const RECT& rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

void RoundFill(HDC dc, const RECT& rect, int radius, COLORREF color, COLORREF border = CLR_INVALID) {
    HBRUSH brush = CreateSolidBrush(color);
    HPEN pen = CreatePen(border == CLR_INVALID ? PS_NULL : PS_SOLID, 1,
                         border == CLR_INVALID ? color : border);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

void DrawTextSimple(HDC dc, HFONT font, COLORREF color, const std::wstring& text, RECT rect,
                    UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS) {
    HGDIOBJ old = font ? SelectObject(dc, font) : nullptr;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, text.c_str(), -1, &rect, flags | DT_NOPREFIX);
    if (old) SelectObject(dc, old);
}

struct PageState {
    HWND parent{};
    HWND panel{};
    HWND profileList{};
    HWND name{};
    HWND serviceType{};
    HWND apiUrl{};
    HWND apiKey{};
    HWND model{};
    HWND addButton{};
    HWND testButton{};
    HWND saveButton{};
    HWND setDefaultButton{};
    HWND deleteButton{};
    HWND revealButton{};
    HWND copyButton{};
    HWND probeModelsButton{};

    HFONT titleFont{};
    HFONT headingFont{};
    HFONT bodyFont{};
    HFONT smallFont{};
    HBRUSH editBrush{};

    ApiProfileStore store;
    std::vector<ApiProfile> profiles;
    std::size_t selected{};
    bool revealingKey{};
    bool statusOk{true};
    std::wstring statusMessage{L"新增一个 API 配置，或从左侧选择已有配置。"};
    L3Agent agent;

    ~PageState() {
        if (panel && IsWindow(panel)) DestroyWindow(panel);
        for (HFONT font : {titleFont, headingFont, bodyFont, smallFont}) {
            if (font) DeleteObject(font);
        }
        if (editBrush) DeleteObject(editBrush);
    }

    int S(int px) const {
        const UINT dpi = parent ? GetDpiForWindow(parent) : USER_DEFAULT_SCREEN_DPI;
        return MulDiv(px, static_cast<int>(dpi ? dpi : USER_DEFAULT_SCREEN_DPI), USER_DEFAULT_SCREEN_DPI);
    }

    HFONT MakeFont(int px, int weight) const {
        return CreateFontW(-S(px), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Text");
    }

    void RebuildFonts() {
        for (HFONT* font : {&titleFont, &headingFont, &bodyFont, &smallFont}) {
            if (*font) { DeleteObject(*font); *font = nullptr; }
        }
        titleFont = MakeFont(26, FW_SEMIBOLD);
        headingFont = MakeFont(15, FW_SEMIBOLD);
        bodyFont = MakeFont(13, FW_NORMAL);
        smallFont = MakeFont(11, FW_NORMAL);
        for (HWND control : {profileList, name, serviceType, apiUrl, apiKey, model, addButton,
                             testButton, saveButton, setDefaultButton, deleteButton,
                             revealButton, copyButton, probeModelsButton}) {
            if (control) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(bodyFont), TRUE);
        }
        if (profileList) SendMessageW(profileList, LB_SETITEMHEIGHT, 0, S(72));
    }

    bool HasSelection() const {
        return !profiles.empty() && selected < profiles.size();
    }

    ApiProfile& Current() { return profiles[selected]; }
    const ApiProfile& Current() const { return profiles[selected]; }

    bool ProfileConfigured(const ApiProfile& profile) const {
        if (profile.baseUrl.empty() || profile.model.empty()) return false;
        return !profile.NeedsKey() || store.HasKey(profile);
    }

    void SetStatus(std::wstring text, bool ok) {
        statusMessage = std::move(text);
        statusOk = ok;
        InvalidateRect(panel, nullptr, FALSE);
    }

    void EnableForm(bool enabled) {
        for (HWND control : {name, serviceType, apiUrl, apiKey, model, testButton, saveButton,
                             setDefaultButton, deleteButton, revealButton, copyButton,
                             probeModelsButton}) {
            if (control) EnableWindow(control, enabled ? TRUE : FALSE);
        }
    }

    void RebuildList() {
        SendMessageW(profileList, LB_RESETCONTENT, 0, 0);
        for (std::size_t i = 0; i < profiles.size(); ++i) {
            const LRESULT index = SendMessageW(profileList, LB_ADDSTRING, 0, static_cast<LPARAM>(i));
            if (index != LB_ERR)
                SendMessageW(profileList, LB_SETITEMDATA, static_cast<WPARAM>(index), static_cast<LPARAM>(i));
        }
        if (HasSelection()) SendMessageW(profileList, LB_SETCURSEL, selected, 0);
        InvalidateRect(profileList, nullptr, FALSE);
        InvalidateRect(panel, nullptr, FALSE);
    }

    void LoadProfiles() {
        const std::wstring preferred = HasSelection() ? Current().id : L"";
        profiles = store.Load();
        if (profiles.empty()) {
            selected = 0;
            RebuildList();
            EnableForm(false);
            SetWindowTextW(name, L"");
            SetWindowTextW(apiUrl, L"");
            SetWindowTextW(apiKey, L"");
            SetWindowTextW(model, L"");
            SetStatus(L"还没有 API 配置，点击“新增配置”开始。", true);
            return;
        }

        auto it = std::find_if(profiles.begin(), profiles.end(), [&](const ApiProfile& p) {
            return !preferred.empty() && _wcsicmp(p.id.c_str(), preferred.c_str()) == 0;
        });
        if (it == profiles.end()) {
            it = std::find_if(profiles.begin(), profiles.end(), [](const ApiProfile& p) { return p.isDefault; });
        }
        selected = it == profiles.end() ? 0 : static_cast<std::size_t>(std::distance(profiles.begin(), it));
        RebuildList();
        LoadForm();
    }

    void LoadForm() {
        if (!HasSelection()) { EnableForm(false); return; }
        EnableForm(true);
        const auto& profile = Current();
        SetWindowTextW(name, profile.name.c_str());
        SetWindowTextW(apiUrl, profile.baseUrl.c_str());
        SetWindowTextW(model, profile.model.c_str());

        SendMessageW(serviceType, CB_SETCURSEL, 0, 0);
        const int count = static_cast<int>(SendMessageW(serviceType, CB_GETCOUNT, 0, 0));
        for (int i = 0; i < count; ++i) {
            wchar_t buffer[256]{};
            SendMessageW(serviceType, CB_GETLBTEXT, i, reinterpret_cast<LPARAM>(buffer));
            if (_wcsicmp(buffer, profile.serviceType.c_str()) == 0) {
                SendMessageW(serviceType, CB_SETCURSEL, i, 0);
                break;
            }
        }

        revealingKey = false;
        SendMessageW(apiKey, EM_SETPASSWORDCHAR, L'●', 0);
        const std::wstring stored = store.Key(profile);
        SetWindowTextW(apiKey, stored.empty() ? L"" : kStoredKeyMask);
        if (!stored.empty() && !HeaderSafeSecret(stored)) {
            SetStatus(L"这个配置的 API Key 来自旧版本且格式异常，请重新粘贴 API Key 后保存。", false);
        } else {
            SetStatus(profile.isDefault ? L"当前默认配置" :
                      (profile.lastMessage.empty() ? L"配置已加载。" : profile.lastMessage),
                      profile.lastMessage.empty() || profile.lastOk);
        }
    }

    ApiProfile FormProfile() const {
        ApiProfile profile = Current();
        profile.name = WindowText(name);
        profile.baseUrl = WindowText(apiUrl);
        profile.model = WindowText(model);
        const int typeIndex = static_cast<int>(SendMessageW(serviceType, CB_GETCURSEL, 0, 0));
        if (typeIndex != CB_ERR) {
            wchar_t buffer[256]{};
            SendMessageW(serviceType, CB_GETLBTEXT, typeIndex, reinterpret_cast<LPARAM>(buffer));
            profile.serviceType = buffer;
        }
        return profile;
    }

    std::wstring FormKey() const {
        const std::wstring value = WindowText(apiKey);
        if (value.empty() || value == kStoredKeyMask) return store.Key(Current());
        return value;
    }

    bool ValidateDraft(ApiProfile& profile, std::wstring& key) {
        if (profile.name.empty()) profile.name = L"API 配置";
        if (profile.baseUrl.empty()) {
            SetStatus(L"请输入 Base URL。", false);
            SetFocus(apiUrl);
            return false;
        }
        if (profile.model.empty()) {
            SetStatus(L"请输入 Model，或点击“探测模型”。", false);
            SetFocus(model);
            return false;
        }
        key = FormKey();
        if (profile.NeedsKey() && key.empty()) {
            SetStatus(L"请输入 API Key。", false);
            SetFocus(apiKey);
            return false;
        }
        if (!key.empty() && !HeaderSafeSecret(key)) {
            SetStatus(L"API Key 包含非 HTTP Header 安全字符，请重新粘贴正确的 Key。", false);
            SetFocus(apiKey);
            return false;
        }
        return true;
    }

    bool SaveCurrent(bool quiet = false) {
        if (!HasSelection()) return false;
        ApiProfile profile = FormProfile();
        std::wstring key;
        if (!ValidateDraft(profile, key)) return false;

        const std::wstring field = WindowText(apiKey);
        if (!field.empty() && field != kStoredKeyMask) {
            if (!store.SaveKey(profile, field)) {
                SetStatus(L"API Key 保存到 Windows Credential Manager 失败。", false);
                return false;
            }
        }

        profiles[selected] = profile;
        store.Save(profile);
        LoadForm();
        if (!quiet) SetStatus(L"配置已保存。", true);
        return true;
    }

    void SelectListItem() {
        const LRESULT index = SendMessageW(profileList, LB_GETCURSEL, 0, 0);
        if (index == LB_ERR) return;
        const LRESULT data = SendMessageW(profileList, LB_GETITEMDATA, static_cast<WPARAM>(index), 0);
        if (data == LB_ERR || data < 0 || static_cast<std::size_t>(data) >= profiles.size()) return;
        selected = static_cast<std::size_t>(data);
        LoadForm();
        InvalidateRect(panel, nullptr, FALSE);
    }

    void NewProfile() {
        ApiProfile profile;
        profile.id = L"custom-" + std::to_wstring(GetTickCount64());
        profile.name = L"新 API 配置";
        profile.lastMessage = L"填写 Base URL、API Key，然后探测或填写 Model。";
        profiles.push_back(std::move(profile));
        selected = profiles.size() - 1;
        RebuildList();
        LoadForm();
        SetFocus(name);
    }

    void ProbeModels() {
        if (!HasSelection()) return;
        ApiProfile profile = FormProfile();
        const std::wstring key = FormKey();
        if (profile.baseUrl.empty()) {
            SetStatus(L"请先填写 Base URL。", false);
            SetFocus(apiUrl);
            return;
        }
        if (profile.NeedsKey() && key.empty()) {
            SetStatus(L"请先填写 API Key。", false);
            SetFocus(apiKey);
            return;
        }
        if (!key.empty() && !HeaderSafeSecret(key)) {
            SetStatus(L"API Key 格式异常，请重新粘贴正确的 Key。", false);
            SetFocus(apiKey);
            return;
        }

        EnableWindow(probeModelsButton, FALSE);
        SetStatus(L"正在探测可用模型…", true);
        UpdateWindow(panel);
        const ModelProbeResult probe = agent.ProbeModels(profile.baseUrl, key, false);
        EnableWindow(probeModelsButton, TRUE);

        if (!probe.ok) {
            SetStatus(probe.message.empty() ? L"模型探测失败，请检查地址、密钥和网络。" : probe.message, false);
            return;
        }

        std::wstring detected = probe.recommendedModel;
        if (detected.empty() && !probe.models.empty()) detected = probe.models.front();
        if (detected.empty()) {
            SetStatus(L"连接成功，但服务没有返回模型列表；Model 可以手动填写。", true);
            return;
        }

        SetWindowTextW(model, detected.c_str());
        const std::size_t count = probe.models.empty() ? 1 : probe.models.size();
        SetStatus(L"探测到 " + std::to_wstring(count) + L" 个模型，已填入 " + detected + L"。", true);
    }

    void TestConnection() {
        if (!HasSelection()) return;
        ApiProfile profile = FormProfile();
        std::wstring key = FormKey();
        if (profile.baseUrl.empty()) {
            SetStatus(L"请先填写 Base URL。", false);
            return;
        }
        if (profile.NeedsKey() && key.empty()) {
            SetStatus(L"请先填写 API Key。", false);
            return;
        }
        if (!key.empty() && !HeaderSafeSecret(key)) {
            SetStatus(L"API Key 格式异常，请重新粘贴正确的 Key。", false);
            return;
        }

        EnableWindow(testButton, FALSE);
        SetStatus(L"正在测试连接…", true);
        UpdateWindow(panel);
        const ModelProbeResult probe = agent.ProbeModels(profile.baseUrl, key, false);
        EnableWindow(testButton, TRUE);

        profile.lastOk = probe.ok;
        profile.lastMessage = probe.ok ? L"连接正常" :
            (probe.message.empty() ? L"连接测试失败。" : probe.message);
        if (probe.ok && profile.model.empty()) {
            profile.model = probe.recommendedModel;
            if (profile.model.empty() && !probe.models.empty()) profile.model = probe.models.front();
            SetWindowTextW(model, profile.model.c_str());
        }
        profiles[selected].lastOk = profile.lastOk;
        profiles[selected].lastMessage = profile.lastMessage;
        store.Save(profiles[selected]);
        SetStatus(profile.lastMessage, probe.ok);
        InvalidateRect(profileList, nullptr, FALSE);
    }

    void SetDefault() {
        if (!SaveCurrent(true)) return;
        for (auto& profile : profiles) profile.isDefault = false;
        profiles[selected].isDefault = true;
        profiles[selected].lastMessage = L"当前默认配置";
        store.SaveAll(profiles);
        agent.ReloadConfig();
        SetStatus(L"已设为默认配置。Pi Agent、DeepSeek Harness 和 Direct Model 会读取这份配置。", true);
        InvalidateRect(profileList, nullptr, FALSE);
    }

    void DeleteProfile() {
        if (!HasSelection()) return;
        const ApiProfile removing = Current();
        store.Remove(removing);
        profiles.erase(profiles.begin() + static_cast<std::ptrdiff_t>(selected));
        if (profiles.empty()) {
            selected = 0;
            RebuildList();
            EnableForm(false);
            SetStatus(L"配置已删除。", true);
            return;
        }
        if (selected >= profiles.size()) selected = profiles.size() - 1;
        RebuildList();
        LoadForm();
        SetStatus(removing.isDefault ? L"默认配置已删除，请选择另一项设为默认。" : L"配置已删除。", true);
    }

    void ToggleReveal() {
        if (!HasSelection()) return;
        const std::wstring stored = store.Key(Current());
        if (WindowText(apiKey) == kStoredKeyMask && !stored.empty()) SetWindowTextW(apiKey, stored.c_str());
        revealingKey = !revealingKey;
        SendMessageW(apiKey, EM_SETPASSWORDCHAR, revealingKey ? 0 : L'●', 0);
        InvalidateRect(apiKey, nullptr, TRUE);
    }

    void CopyKey() {
        if (!HasSelection()) return;
        const std::wstring key = FormKey();
        if (key.empty()) {
            SetStatus(L"当前配置没有 API Key。", false);
            return;
        }
        if (!OpenClipboard(panel)) return;
        EmptyClipboard();
        const SIZE_T bytes = (key.size() + 1) * sizeof(wchar_t);
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (memory) {
            void* target = GlobalLock(memory);
            if (target) {
                memcpy(target, key.c_str(), bytes);
                GlobalUnlock(memory);
                SetClipboardData(CF_UNICODETEXT, memory);
                memory = nullptr;
            }
        }
        if (memory) GlobalFree(memory);
        CloseClipboard();
        SetStatus(L"API Key 已复制。", true);
    }

    void Layout() {
        if (!parent || !panel) return;
        RECT client{};
        GetClientRect(parent, &client);
        const int sidebar = S(208);
        const int top = S(58);
        SetWindowPos(panel, nullptr, sidebar, top,
                     std::max(1, static_cast<int>(client.right) - sidebar),
                     std::max(1, static_cast<int>(client.bottom) - top),
                     SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOREDRAW);

        RECT area{};
        GetClientRect(panel, &area);
        const int width = std::max(1, static_cast<int>(area.right));
        const int height = std::max(1, static_cast<int>(area.bottom));
        const int margin = std::max(S(18), width / 40);
        const int contentW = std::max(S(560), width - margin * 2);
        const int headerH = S(82);
        const int bodyTop = margin + headerH;
        const int bodyH = std::max(S(360), height - bodyTop - margin);
        const int gap = S(16);
        const int leftW = std::clamp(contentW * 38 / 100, S(300), S(430));
        const int rightX = margin + leftW + gap;
        const int rightW = std::max(S(360), contentW - leftW - gap);

        auto place = [&](HWND hwnd, int x, int y, int w, int h) {
            if (!hwnd) return;
            SetWindowPos(hwnd, nullptr, x, y, std::max(1, w), std::max(1, h),
                         SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOREDRAW);
        };

        place(addButton, margin + contentW - S(150), margin + S(4), S(150), S(42));
        place(profileList, margin + S(14), bodyTop + S(52), leftW - S(28), bodyH - S(70));

        const int labelW = S(112);
        const int fieldX = rightX + S(18) + labelW;
        const int fieldW = rightW - S(36) - labelW;
        const int rowH = S(36);
        const int rowGap = S(18);
        int y = bodyTop + S(58);
        place(name, fieldX, y, fieldW, rowH); y += rowH + rowGap;
        place(serviceType, fieldX, y, fieldW, S(180)); y += rowH + rowGap;
        place(apiUrl, fieldX, y, fieldW, rowH); y += rowH + rowGap;
        place(apiKey, fieldX, y, std::max(S(120), fieldW - S(90)), rowH);
        place(revealButton, fieldX + fieldW - S(84), y, S(36), rowH);
        place(copyButton, fieldX + fieldW - S(44), y, S(44), rowH); y += rowH + rowGap;
        const int probeW = S(104);
        const int modelGap = S(8);
        place(model, fieldX, y, std::max(S(120), fieldW - probeW - modelGap), rowH);
        place(probeModelsButton, fieldX + fieldW - probeW, y, probeW, rowH);

        const int actionY = bodyTop + bodyH - S(66);
        int actionX = rightX + S(18);
        place(testButton, actionX, actionY, S(112), S(40)); actionX += S(120);
        place(saveButton, actionX, actionY, S(112), S(40)); actionX += S(120);
        place(setDefaultButton, actionX, actionY, S(126), S(40));
        place(deleteButton, rightX + rightW - S(112), actionY, S(94), S(40));

        RedrawWindow(panel, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
    }

    void DrawActionButton(const DRAWITEMSTRUCT& draw) {
        RECT rect = draw.rcItem;
        InflateRect(&rect, -S(1), -S(1));
        const int id = static_cast<int>(draw.CtlID);
        const bool pressed = (draw.itemState & ODS_SELECTED) != 0;
        const bool disabled = (draw.itemState & ODS_DISABLED) != 0;
        COLORREF background = RGB(249, 252, 255);
        COLORREF border = RGB(199, 216, 240);
        COLORREF textColor = RGB(45, 92, 166);

        if (id == kNewId || id == kSaveId || id == kSetDefaultId) {
            background = pressed ? RGB(31, 102, 226) : RGB(43, 118, 246);
            border = background;
            textColor = RGB(255, 255, 255);
        } else if (id == kDeleteId) {
            background = pressed ? RGB(255, 241, 242) : RGB(253, 254, 255);
            border = RGB(247, 205, 209);
            textColor = RGB(224, 52, 61);
        } else if (id == kRevealId || id == kCopyId || id == kProbeModelsId) {
            background = pressed ? RGB(235, 243, 255) : RGB(248, 251, 255);
            border = RGB(220, 231, 246);
            textColor = RGB(57, 88, 139);
        }
        if (disabled) {
            background = RGB(242, 245, 249);
            border = RGB(225, 231, 239);
            textColor = RGB(151, 164, 184);
        }

        RoundFill(draw.hDC, rect, S(10), background, border);
        DrawTextSimple(draw.hDC, bodyFont, textColor, WindowText(draw.hwndItem), rect,
                       DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    void DrawProfileItem(const DRAWITEMSTRUCT& draw) {
        if (draw.itemID == static_cast<UINT>(-1) || draw.itemData >= profiles.size()) return;
        RECT rect = draw.rcItem;
        InflateRect(&rect, -S(2), -S(4));
        const auto& profile = profiles[draw.itemData];
        const bool selectedItem = (draw.itemState & ODS_SELECTED) != 0;
        const bool configured = ProfileConfigured(profile);
        RoundFill(draw.hDC, rect, S(14), selectedItem ? RGB(239, 246, 255) : RGB(252, 253, 255),
                  selectedItem ? RGB(44, 116, 255) : RGB(225, 232, 242));

        RECT titleRect{rect.left + S(16), rect.top + S(8), rect.right - S(90), rect.top + S(34)};
        DrawTextSimple(draw.hDC, headingFont, RGB(24, 47, 86), profile.name, titleRect);
        std::wstring subtitle = profile.model.empty() ? profile.baseUrl : profile.model;
        if (subtitle.empty()) subtitle = L"未完成配置";
        RECT subRect{rect.left + S(16), rect.top + S(34), rect.right - S(16), rect.bottom - S(7)};
        DrawTextSimple(draw.hDC, smallFont, RGB(92, 112, 145), subtitle, subRect);

        if (profile.isDefault) {
            RECT badge{rect.right - S(78), rect.top + S(16), rect.right - S(14), rect.top + S(42)};
            RoundFill(draw.hDC, badge, S(8), RGB(231, 240, 255));
            DrawTextSimple(draw.hDC, smallFont, RGB(49, 105, 220), L"默认", badge,
                           DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        } else if (configured) {
            RECT badge{rect.right - S(78), rect.top + S(16), rect.right - S(14), rect.top + S(42)};
            RoundFill(draw.hDC, badge, S(8), RGB(229, 249, 238));
            DrawTextSimple(draw.hDC, smallFont, RGB(28, 160, 92), L"已配置", badge,
                           DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }

    void Paint(HDC dc) {
        RECT client{};
        GetClientRect(panel, &client);
        FillSolid(dc, client, RGB(246, 250, 255));
        const int width = client.right;
        const int height = client.bottom;
        const int margin = std::max(S(18), width / 40);
        const int contentW = std::max(S(560), width - margin * 2);
        const int headerH = S(82);
        const int bodyTop = margin + headerH;
        const int bodyH = std::max(S(360), height - bodyTop - margin);
        const int gap = S(16);
        const int leftW = std::clamp(contentW * 38 / 100, S(300), S(430));
        const int rightX = margin + leftW + gap;
        const int rightW = std::max(S(360), contentW - leftW - gap);

        RECT title{margin, margin - S(2), margin + contentW - S(160), margin + S(34)};
        DrawTextSimple(dc, titleFont, RGB(18, 39, 75), L"API 配置中心", title);
        RECT subtitle{margin, margin + S(34), margin + contentW - S(160), margin + S(60)};
        DrawTextSimple(dc, smallFont, RGB(91, 110, 142),
                       L"只管理服务地址、密钥和模型；Pi Agent / Harness / Direct Model 共用默认配置", subtitle);

        RECT leftPanel{margin, bodyTop, margin + leftW, bodyTop + bodyH};
        RECT rightPanel{rightX, bodyTop, rightX + rightW, bodyTop + bodyH};
        RoundFill(dc, leftPanel, S(18), RGB(253, 254, 255), RGB(231, 237, 247));
        RoundFill(dc, rightPanel, S(18), RGB(253, 254, 255), RGB(231, 237, 247));

        RECT leftHeading{leftPanel.left + S(16), leftPanel.top + S(12), leftPanel.right - S(16), leftPanel.top + S(40)};
        DrawTextSimple(dc, headingFont, RGB(28, 49, 83), L"已保存配置", leftHeading);
        RECT rightHeading{rightPanel.left + S(18), rightPanel.top + S(12), rightPanel.right - S(18), rightPanel.top + S(40)};
        DrawTextSimple(dc, headingFont, RGB(28, 49, 83), L"基本配置", rightHeading);

        const int labelW = S(112);
        int y = bodyTop + S(58);
        for (const auto& label : {L"配置名称", L"接口类型", L"Base URL", L"API Key", L"Model"}) {
            RECT labelRect{rightX + S(18), y, rightX + S(18) + labelW - S(10), y + S(36)};
            DrawTextSimple(dc, bodyFont, RGB(68, 88, 124), label, labelRect);
            y += S(54);
        }

        const int actionY = bodyTop + bodyH - S(66);
        RECT status{rightX + S(18), actionY - S(48), rightX + rightW - S(18), actionY - S(12)};
        RoundFill(dc, status, S(10), statusOk ? RGB(239, 251, 244) : RGB(255, 241, 242));
        DrawTextSimple(dc, smallFont, statusOk ? RGB(34, 161, 96) : RGB(210, 62, 68),
                       std::wstring(L"●  ") + statusMessage, status);

        if (profiles.empty()) {
            RECT empty{leftPanel.left + S(24), leftPanel.top + S(72), leftPanel.right - S(24), leftPanel.bottom - S(24)};
            DrawTextSimple(dc, bodyFont, RGB(117, 133, 158), L"还没有配置\n点击右上角“新增配置”开始",
                           empty, DT_CENTER | DT_VCENTER | DT_WORDBREAK);
        }
    }
};

PageState* StateFor(HWND parent) {
    return parent ? reinterpret_cast<PageState*>(GetPropW(parent, kStateProperty)) : nullptr;
}

LRESULT CALLBACK PageProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    PageState* state = reinterpret_cast<PageState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = static_cast<PageState*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    if (!state) return DefWindowProcW(window, message, wParam, lParam);

    switch (message) {
    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        const int code = HIWORD(wParam);
        if (id == kProfileListId && code == LBN_SELCHANGE) { state->SelectListItem(); return 0; }
        if (code == BN_CLICKED) {
            if (id == kNewId) { state->NewProfile(); return 0; }
            if (id == kProbeModelsId) { state->ProbeModels(); return 0; }
            if (id == kTestId) { state->TestConnection(); return 0; }
            if (id == kSaveId) { state->SaveCurrent(); return 0; }
            if (id == kSetDefaultId) { state->SetDefault(); return 0; }
            if (id == kDeleteId) { state->DeleteProfile(); return 0; }
            if (id == kRevealId) { state->ToggleReveal(); return 0; }
            if (id == kCopyId) { state->CopyKey(); return 0; }
        }
        break;
    }
    case WM_DRAWITEM: {
        const auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (draw && draw->CtlID == kProfileListId) {
            state->DrawProfileItem(*draw);
            return TRUE;
        }
        if (draw && draw->CtlType == ODT_BUTTON) {
            state->DrawActionButton(*draw);
            return TRUE;
        }
        break;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        state->Paint(dc);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetBkColor(dc, RGB(253, 254, 255));
        SetTextColor(dc, RGB(28, 49, 83));
        return reinterpret_cast<LRESULT>(state->editBrush);
    }
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void DestroyState(HWND parent) {
    auto* state = StateFor(parent);
    if (!state) return;
    RemovePropW(parent, kStateProperty);
    delete state;
}

LRESULT CALLBACK ParentSubclass(HWND parent, UINT message, WPARAM wParam, LPARAM lParam,
                                UINT_PTR, DWORD_PTR) {
    if (message == WM_COMMAND && HIWORD(wParam) == BN_CLICKED) {
        const int id = LOWORD(wParam);
        if (id >= kFirstNavId && id < kAiNavId) HideDesktopAiSettingsPage(parent);
    }
    const LRESULT result = DefSubclassProc(parent, message, wParam, lParam);
    auto* state = StateFor(parent);
    if (state && message == WM_DPICHANGED) state->RebuildFonts();
    if (state && (message == WM_SIZE || message == WM_DPICHANGED || message == WM_SHOWWINDOW)) {
        state->Layout();
        if (state->panel && IsWindowVisible(state->panel))
            SetWindowPos(state->panel, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(parent, ParentSubclass, kParentSubclassId);
        DestroyState(parent);
    }
    return result;
}

bool CreatePage(PageState& state) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpfnWndProc = PageProc;
    wc.lpszClassName = kPageClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

    state.editBrush = CreateSolidBrush(RGB(253, 254, 255));
    state.panel = CreateWindowExW(WS_EX_CONTROLPARENT | WS_EX_COMPOSITED, kPageClass, L"",
                                  WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                                  0, 0, 10, 10, state.parent, nullptr, wc.hInstance, &state);
    if (!state.panel) return false;

    auto edit = [&](int id, DWORD extra = 0) {
        return CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                               WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | extra,
                               0, 0, 10, 10, state.panel, ControlId(id), wc.hInstance, nullptr);
    };
    auto button = [&](const wchar_t* text, int id) {
        return CreateWindowExW(0, L"BUTTON", text,
                               WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                               0, 0, 10, 10, state.panel, ControlId(id), wc.hInstance, nullptr);
    };

    state.profileList = CreateWindowExW(0, L"LISTBOX", L"",
                                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
                                        LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_NOINTEGRALHEIGHT,
                                        0, 0, 10, 10, state.panel, ControlId(kProfileListId), wc.hInstance, nullptr);
    state.name = edit(kNameId);
    state.serviceType = CreateWindowExW(WS_EX_CLIENTEDGE, L"COMBOBOX", L"",
                                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                                        0, 0, 10, 180, state.panel, ControlId(kTypeId), wc.hInstance, nullptr);
    for (const wchar_t* type : {L"OpenAI Compatible", L"Anthropic", L"Google Generative AI", L"Local / Custom"})
        SendMessageW(state.serviceType, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(type));
    state.apiUrl = edit(kApiUrlId);
    state.apiKey = edit(kApiKeyId, ES_PASSWORD);
    state.model = edit(kModelId);
    state.addButton = button(L"＋ 新增配置", kNewId);
    state.testButton = button(L"测试连接", kTestId);
    state.saveButton = button(L"保存", kSaveId);
    state.setDefaultButton = button(L"设为默认", kSetDefaultId);
    state.deleteButton = button(L"删除", kDeleteId);
    state.revealButton = button(L"◉", kRevealId);
    state.copyButton = button(L"复制", kCopyId);
    state.probeModelsButton = button(L"探测模型", kProbeModelsId);

    if (!state.profileList || !state.name || !state.serviceType || !state.apiUrl || !state.apiKey ||
        !state.model || !state.addButton || !state.testButton || !state.saveButton ||
        !state.setDefaultButton || !state.deleteButton || !state.revealButton || !state.copyButton ||
        !state.probeModelsButton)
        return false;

    SendMessageW(state.name, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"例如 公司 DeepSeek"));
    SendMessageW(state.apiUrl, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"https://api.example.com/v1"));
    SendMessageW(state.apiKey, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"粘贴 API Key"));
    SendMessageW(state.model, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"例如 deepseek-chat"));
    state.RebuildFonts();
    state.LoadProfiles();
    return true;
}

} // namespace

bool ShowDesktopAiSettingsPage(HWND desktopSettingsWindow) {
    if (!desktopSettingsWindow || !IsWindow(desktopSettingsWindow)) return false;
    PageState* state = StateFor(desktopSettingsWindow);
    if (!state) {
        auto owned = std::make_unique<PageState>();
        owned->parent = desktopSettingsWindow;
        if (!CreatePage(*owned)) return false;
        state = owned.release();
        SetPropW(desktopSettingsWindow, kStateProperty, reinterpret_cast<HANDLE>(state));
        SetWindowSubclass(desktopSettingsWindow, ParentSubclass, kParentSubclassId, 0);
    }
    state->LoadProfiles();
    state->Layout();
    ShowWindow(state->panel, SW_SHOW);
    SetWindowPos(state->panel, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    InvalidateRect(state->panel, nullptr, FALSE);
    return true;
}

void HideDesktopAiSettingsPage(HWND desktopSettingsWindow) {
    if (auto* state = StateFor(desktopSettingsWindow); state && state->panel)
        ShowWindow(state->panel, SW_HIDE);
}

bool DesktopAiSettingsPageVisible(HWND desktopSettingsWindow) {
    const auto* state = StateFor(desktopSettingsWindow);
    return state && state->panel && IsWindowVisible(state->panel);
}

} // namespace miaodesk::wallpaper
