#include "turingdesk/DesktopAiSettingsPage.h"
#include "turingdesk/L3Agent.h"

#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wincred.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace turingdesk::wallpaper {
namespace {

constexpr wchar_t kPageClass[] = L"TuringDesk.Native.ApiConfigurationCenter";
constexpr wchar_t kStateProperty[] = L"TuringDesk.ApiConfigurationCenter.State";
constexpr UINT_PTR kParentSubclassId = 0x54444150; // TDAP
constexpr int kFirstNavId = 6110;
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
constexpr wchar_t kActiveCredentialTarget[] = L"TuringDesk/ModelApiKey";

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
    PWSTR raw = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &raw)) && raw) {
        fs::path root(raw);
        CoTaskMemFree(raw);
        std::error_code ec;
        root /= L"TuringDesk";
        fs::create_directories(root, ec);
        return root;
    }
    return fs::temp_directory_path() / L"TuringDesk";
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
    GetPrivateProfileStringW(section, key, fallback, buffer.data(), static_cast<DWORD>(buffer.size()), path.c_str());
    return buffer.data();
}

void WriteIni(const fs::path& path, const wchar_t* section, const wchar_t* key, const std::wstring& value) {
    EnsureUnicodeIni(path);
    WritePrivateProfileStringW(section, key, value.c_str(), path.c_str());
}

bool ParseBool(std::wstring value, bool fallback = false) {
    if (value.empty()) return fallback;
    return value == L"1" || _wcsicmp(value.c_str(), L"true") == 0 || _wcsicmp(value.c_str(), L"yes") == 0;
}

int ParseInt(const std::wstring& value, int fallback) {
    if (value.empty()) return fallback;
    wchar_t* end = nullptr;
    const long parsed = wcstol(value.c_str(), &end, 10);
    return end && end != value.c_str() ? static_cast<int>(parsed) : fallback;
}

std::wstring CredentialTarget(std::wstring_view id) {
    return L"TuringDesk/ApiProfile/" + std::wstring(id);
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
    if (value.empty()) return true;
    CREDENTIALW credential{};
    std::wstring mutableTarget(target);
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = mutableTarget.data();
    credential.CredentialBlobSize = static_cast<DWORD>(value.size() * sizeof(wchar_t));
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<wchar_t*>(value.data()));
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    credential.UserName = const_cast<wchar_t*>(L"TuringDesk");
    return CredWriteW(&credential, 0) != FALSE;
}

void DeleteCredential(std::wstring_view target) {
    CredDeleteW(std::wstring(target).c_str(), CRED_TYPE_GENERIC, 0);
}

struct ApiProfile {
    std::wstring id;
    std::wstring name;
    std::wstring serviceType{L"OpenAI Compatible"};
    std::wstring baseUrl;
    std::wstring model;
    int timeoutSeconds{60};
    int temperatureTenths{7};
    int retries{2};
    bool streaming{true};
    bool tools{true};
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

    std::vector<ApiProfile> Load(const L3Agent& agent) {
        std::vector<ApiProfile> profiles;
        std::array<wchar_t, 16384> sections{};
        const DWORD size = GetPrivateProfileSectionNamesW(sections.data(), static_cast<DWORD>(sections.size()), path_.c_str());
        if (size > 0) {
            for (const wchar_t* cursor = sections.data(); *cursor; cursor += wcslen(cursor) + 1) {
                if (wcsncmp(cursor, L"profile:", 8) != 0) continue;
                ApiProfile profile;
                profile.id = cursor + 8;
                profile.name = ReadIni(path_, cursor, L"name", L"API 配置");
                profile.serviceType = ReadIni(path_, cursor, L"type", L"OpenAI Compatible");
                profile.baseUrl = ReadIni(path_, cursor, L"baseUrl");
                profile.model = ReadIni(path_, cursor, L"model");
                profile.timeoutSeconds = ParseInt(ReadIni(path_, cursor, L"timeout"), 60);
                profile.temperatureTenths = ParseInt(ReadIni(path_, cursor, L"temperatureTenths"), 7);
                profile.retries = ParseInt(ReadIni(path_, cursor, L"retries"), 2);
                profile.streaming = ParseBool(ReadIni(path_, cursor, L"streaming", L"1"), true);
                profile.tools = ParseBool(ReadIni(path_, cursor, L"tools", L"1"), true);
                profile.isDefault = ParseBool(ReadIni(path_, cursor, L"default"));
                profile.lastOk = ParseBool(ReadIni(path_, cursor, L"lastOk"));
                profile.lastMessage = ReadIni(path_, cursor, L"lastMessage");
                profile.builtIn = ParseBool(ReadIni(path_, cursor, L"builtIn"));
                profiles.push_back(std::move(profile));
            }
        }
        if (profiles.empty()) {
            profiles = DefaultTemplates(agent);
            SaveAll(profiles);
        }
        return profiles;
    }

    void Save(const ApiProfile& profile) {
        const std::wstring section = L"profile:" + profile.id;
        WriteIni(path_, section.c_str(), L"name", profile.name);
        WriteIni(path_, section.c_str(), L"type", profile.serviceType);
        WriteIni(path_, section.c_str(), L"baseUrl", profile.baseUrl);
        WriteIni(path_, section.c_str(), L"model", profile.model);
        WriteIni(path_, section.c_str(), L"timeout", std::to_wstring(profile.timeoutSeconds));
        WriteIni(path_, section.c_str(), L"temperatureTenths", std::to_wstring(profile.temperatureTenths));
        WriteIni(path_, section.c_str(), L"retries", std::to_wstring(profile.retries));
        WriteIni(path_, section.c_str(), L"streaming", profile.streaming ? L"1" : L"0");
        WriteIni(path_, section.c_str(), L"tools", profile.tools ? L"1" : L"0");
        WriteIni(path_, section.c_str(), L"default", profile.isDefault ? L"1" : L"0");
        WriteIni(path_, section.c_str(), L"lastOk", profile.lastOk ? L"1" : L"0");
        WriteIni(path_, section.c_str(), L"lastMessage", profile.lastMessage);
        WriteIni(path_, section.c_str(), L"builtIn", profile.builtIn ? L"1" : L"0");
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
    std::vector<ApiProfile> DefaultTemplates(const L3Agent& agent) {
        std::vector<ApiProfile> profiles{
            {L"deepseek", L"DeepSeek 官方", L"OpenAI Compatible", L"https://api.deepseek.com/v1", L"deepseek-chat", 60, 7, 2, true, true, false, false, L"等待配置", true},
            {L"openai-compatible", L"OpenAI Compatible", L"OpenAI Compatible", L"", L"", 60, 7, 2, true, true, false, false, L"Base URL/API Key 未填写", true},
            {L"anthropic", L"Anthropic", L"Anthropic", L"https://api.anthropic.com/v1", L"", 60, 7, 2, true, true, false, false, L"等待配置", true},
            {L"local-model", L"本地模型", L"OpenAI Compatible", L"http://127.0.0.1:11434/v1", L"", 60, 7, 1, true, true, false, false, L"等待配置", true},
            {L"custom", L"自定义服务", L"OpenAI Compatible", L"", L"", 60, 7, 2, true, true, false, false, L"等待配置", true},
        };

        const auto& config = agent.Config();
        std::wstring currentApi = agent.CurrentApiUrl();
        if (currentApi.empty()) currentApi = config.baseUrl;
        if (!currentApi.empty() || !config.model.empty() || agent.HasStoredApiKey()) {
            std::size_t selected = 0;
            if (currentApi.find(L"deepseek") == std::wstring::npos) selected = 4;
            auto& current = profiles[selected];
            current.baseUrl = currentApi;
            current.model = config.model;
            current.name = selected == 0 ? L"DeepSeek 官方" : L"当前 API";
            current.isDefault = true;
            current.lastOk = true;
            current.lastMessage = L"已从现有妙喵 AI 配置导入";
            const auto activeKey = ReadCredential(kActiveCredentialTarget);
            if (!activeKey.empty()) WriteCredential(CredentialTarget(current.id), activeKey);
        }
        return profiles;
    }

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
    HWND timeout{};
    HWND temperature{};
    HWND defaultCheck{};
    HWND streamingCheck{};
    HWND toolsCheck{};
    HWND retries{};
    HWND addButton{};
    HWND testButton{};
    HWND saveButton{};
    HWND deleteButton{};
    HWND revealButton{};
    HWND copyButton{};

    HFONT titleFont{};
    HFONT headingFont{};
    HFONT bodyFont{};
    HFONT smallFont{};
    HFONT metricFont{};
    HBRUSH editBrush{};

    ApiProfileStore store;
    std::vector<ApiProfile> profiles;
    std::size_t selected{};
    bool revealingKey{};
    bool statusOk{true};
    std::wstring statusMessage{L"选择或新增一个 API 配置。"};
    L3Agent agent;

    ~PageState() {
        if (panel && IsWindow(panel)) DestroyWindow(panel);
        for (HFONT font : {titleFont, headingFont, bodyFont, smallFont, metricFont}) {
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
        for (HFONT* font : {&titleFont, &headingFont, &bodyFont, &smallFont, &metricFont}) {
            if (*font) { DeleteObject(*font); *font = nullptr; }
        }
        titleFont = MakeFont(26, FW_SEMIBOLD);
        headingFont = MakeFont(15, FW_SEMIBOLD);
        bodyFont = MakeFont(13, FW_NORMAL);
        smallFont = MakeFont(11, FW_NORMAL);
        metricFont = MakeFont(24, FW_SEMIBOLD);
        for (HWND control : {profileList, name, serviceType, apiUrl, apiKey, model, timeout, temperature,
                             defaultCheck, streamingCheck, toolsCheck, retries, addButton, testButton,
                             saveButton, deleteButton, revealButton, copyButton}) {
            if (control) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(bodyFont), TRUE);
        }
        if (profileList) SendMessageW(profileList, LB_SETITEMHEIGHT, 0, S(72));
    }

    ApiProfile& Current() { return profiles[std::min(selected, profiles.size() - 1)]; }
    const ApiProfile& Current() const { return profiles[std::min(selected, profiles.size() - 1)]; }

    bool ProfileConfigured(const ApiProfile& profile) const {
        if (profile.baseUrl.empty() || profile.model.empty()) return false;
        return !profile.NeedsKey() || store.HasKey(profile);
    }

    void SetStatus(std::wstring text, bool ok) {
        statusMessage = std::move(text);
        statusOk = ok;
        InvalidateRect(panel, nullptr, FALSE);
    }

    void RebuildList() {
        SendMessageW(profileList, LB_RESETCONTENT, 0, 0);
        for (std::size_t i = 0; i < profiles.size(); ++i) {
            const LRESULT index = SendMessageW(profileList, LB_ADDSTRING, 0, static_cast<LPARAM>(i));
            if (index != LB_ERR) SendMessageW(profileList, LB_SETITEMDATA, static_cast<WPARAM>(index), static_cast<LPARAM>(i));
        }
        if (!profiles.empty()) {
            selected = std::min(selected, profiles.size() - 1);
            SendMessageW(profileList, LB_SETCURSEL, selected, 0);
        }
        InvalidateRect(profileList, nullptr, FALSE);
        InvalidateRect(panel, nullptr, FALSE);
    }

    void LoadProfiles() {
        profiles = store.Load(agent);
        if (profiles.empty()) return;
        const auto it = std::find_if(profiles.begin(), profiles.end(), [](const ApiProfile& p) { return p.isDefault; });
        selected = it == profiles.end() ? 0 : static_cast<std::size_t>(std::distance(profiles.begin(), it));
        RebuildList();
        LoadForm();
    }

    void LoadForm() {
        if (profiles.empty()) return;
        const auto& profile = Current();
        SetWindowTextW(name, profile.name.c_str());
        SetWindowTextW(apiUrl, profile.baseUrl.c_str());
        SetWindowTextW(model, profile.model.c_str());
        SetWindowTextW(timeout, std::to_wstring(profile.timeoutSeconds).c_str());
        wchar_t temp[16]{};
        swprintf_s(temp, L"%.1f", profile.temperatureTenths / 10.0);
        SetWindowTextW(temperature, temp);
        SetWindowTextW(retries, std::to_wstring(profile.retries).c_str());
        SendMessageW(defaultCheck, BM_SETCHECK, profile.isDefault ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(streamingCheck, BM_SETCHECK, profile.streaming ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(toolsCheck, BM_SETCHECK, profile.tools ? BST_CHECKED : BST_UNCHECKED, 0);

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
        SetWindowTextW(apiKey, store.HasKey(profile) ? kStoredKeyMask : L"");
        SetStatus(profile.lastMessage.empty() ? L"尚未测试连接。" : profile.lastMessage,
                  profile.lastMessage.empty() || profile.lastOk);
    }

    ApiProfile FormProfile() const {
        ApiProfile profile = Current();
        profile.name = WindowText(name);
        profile.baseUrl = WindowText(apiUrl);
        profile.model = WindowText(model);
        profile.timeoutSeconds = std::clamp(ParseInt(WindowText(timeout), 60), 5, 600);
        const double temp = _wtof(WindowText(temperature).c_str());
        profile.temperatureTenths = std::clamp(static_cast<int>(temp * 10.0 + 0.5), 0, 20);
        profile.retries = std::clamp(ParseInt(WindowText(retries), 2), 0, 5);
        profile.isDefault = SendMessageW(defaultCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
        profile.streaming = SendMessageW(streamingCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
        profile.tools = SendMessageW(toolsCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
        const int typeIndex = static_cast<int>(SendMessageW(serviceType, CB_GETCURSEL, 0, 0));
        if (typeIndex != CB_ERR) {
            wchar_t buffer[256]{};
            SendMessageW(serviceType, CB_GETLBTEXT, typeIndex, reinterpret_cast<LPARAM>(buffer));
            profile.serviceType = buffer;
        }
        return profile;
    }

    std::wstring FormKey() const {
        std::wstring value = WindowText(apiKey);
        if (value.empty() || value == kStoredKeyMask) return store.Key(Current());
        return value;
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
        profile.lastMessage = L"填写服务地址、API Key 和模型后保存。";
        profiles.push_back(std::move(profile));
        selected = profiles.size() - 1;
        RebuildList();
        LoadForm();
        SetFocus(name);
    }

    bool PersistForm(bool requireConnection) {
        if (profiles.empty()) return false;
        ApiProfile profile = FormProfile();
        if (profile.name.empty()) {
            SetStatus(L"请输入配置名称。", false);
            SetFocus(name);
            return false;
        }
        if (profile.baseUrl.empty()) {
            SetStatus(L"请输入 Base URL。", false);
            SetFocus(apiUrl);
            return false;
        }

        std::wstring keyField = WindowText(apiKey);
        if (!keyField.empty() && keyField != kStoredKeyMask) {
            if (!store.SaveKey(profile, keyField)) {
                SetStatus(L"API Key 保存到 Windows Credential Manager 失败。", false);
                return false;
            }
        }
        std::wstring key = store.Key(profile);
        if (profile.NeedsKey() && key.empty()) {
            SetStatus(L"这个服务需要 API Key。", false);
            SetFocus(apiKey);
            return false;
        }

        ModelProbeResult probe;
        if (requireConnection || profile.isDefault || profile.model.empty()) {
            SetStatus(L"正在测试连接并获取模型…", true);
            UpdateWindow(panel);
            probe = agent.ProbeModels(profile.baseUrl, key, false);
            if (!probe.ok) {
                profile.lastOk = false;
                profile.lastMessage = probe.message.empty() ? L"连接测试失败，请检查地址、密钥和网络。" : probe.message;
                profiles[selected] = profile;
                store.Save(profile);
                RebuildList();
                SetStatus(profile.lastMessage, false);
                return false;
            }
            if (profile.model.empty()) profile.model = probe.recommendedModel;
            if (profile.model.empty() && !probe.models.empty()) profile.model = probe.models.front();
            profile.lastOk = true;
            profile.lastMessage = L"最近测试成功 · 连接正常";
        }

        if (profile.model.empty()) {
            SetStatus(L"连接可用，但没有检测到模型，请填写默认模型。", false);
            SetFocus(model);
            return false;
        }

        if (profile.isDefault) {
            if (!probe.ok) probe = agent.ProbeModels(profile.baseUrl, key, false);
            if (!probe.ok) {
                SetStatus(probe.message.empty() ? L"默认服务必须先通过连接测试。" : probe.message, false);
                return false;
            }
            std::wstring reply;
            if (!agent.ApplyModelConfig(probe, profile.model, key, false, reply)) {
                SetStatus(reply.empty() ? L"无法应用默认模型配置。" : reply, false);
                return false;
            }
            for (auto& item : profiles) item.isDefault = false;
            profile.isDefault = true;
        }

        profiles[selected] = profile;
        store.SaveAll(profiles);
        RebuildList();
        LoadForm();
        SetStatus(profile.lastOk ? profile.lastMessage : L"配置已保存。", true);
        return true;
    }

    void TestConnection() {
        if (profiles.empty()) return;
        ApiProfile profile = FormProfile();
        if (profile.baseUrl.empty()) {
            SetStatus(L"请先填写 Base URL。", false);
            return;
        }
        std::wstring key = FormKey();
        if (profile.NeedsKey() && key.empty()) {
            SetStatus(L"请先填写 API Key。", false);
            return;
        }
        EnableWindow(testButton, FALSE);
        SetStatus(L"正在测试连接…", true);
        UpdateWindow(panel);
        const ModelProbeResult probe = agent.ProbeModels(profile.baseUrl, key, false);
        EnableWindow(testButton, TRUE);
        profile.lastOk = probe.ok;
        profile.lastMessage = probe.ok ? L"最近测试成功 · 连接正常" :
            (probe.message.empty() ? L"连接测试失败。" : probe.message);
        if (probe.ok && profile.model.empty()) {
            profile.model = probe.recommendedModel;
            if (profile.model.empty() && !probe.models.empty()) profile.model = probe.models.front();
            SetWindowTextW(model, profile.model.c_str());
        }
        profiles[selected] = profile;
        store.Save(profile);
        RebuildList();
        SetStatus(profile.lastMessage, probe.ok);
    }

    void DeleteProfile() {
        if (profiles.empty()) return;
        const ApiProfile removing = Current();
        store.Remove(removing);
        profiles.erase(profiles.begin() + static_cast<std::ptrdiff_t>(selected));
        if (profiles.empty()) {
            NewProfile();
            return;
        }
        if (selected >= profiles.size()) selected = profiles.size() - 1;
        if (removing.isDefault) {
            auto configured = std::find_if(profiles.begin(), profiles.end(), [&](const ApiProfile& p) { return ProfileConfigured(p); });
            if (configured != profiles.end()) configured->isDefault = true;
            store.SaveAll(profiles);
        }
        RebuildList();
        LoadForm();
        SetStatus(L"配置已删除。", true);
    }

    void ToggleReveal() {
        revealingKey = !revealingKey;
        SendMessageW(apiKey, EM_SETPASSWORDCHAR, revealingKey ? 0 : L'●', 0);
        InvalidateRect(apiKey, nullptr, TRUE);
    }

    void CopyKey() {
        const std::wstring key = FormKey();
        if (key.empty()) {
            SetStatus(L"当前配置没有可复制的 API Key。", false);
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
        SetStatus(L"API Key 已复制到剪贴板。", true);
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
        const int margin = std::max(S(16), width / 40);
        const int contentW = std::max(S(500), width - margin * 2);
        const int headerH = S(72);
        const int statsH = S(82);
        const int bodyTop = margin + headerH + statsH + S(22);
        const int bodyH = std::max(S(360), height - bodyTop - margin);
        const int columnGap = S(14);
        const int leftW = std::clamp(contentW * 39 / 100, S(280), S(430));
        const int rightX = margin + leftW + columnGap;
        const int rightW = std::max(S(320), contentW - leftW - columnGap);

        auto place = [&](HWND hwnd, int x, int y, int w, int h) {
            if (!hwnd) return;
            SetWindowPos(hwnd, nullptr, x, y, std::max(1, w), std::max(1, h),
                         SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOREDRAW);
        };

        place(addButton, margin + contentW - S(132), margin + S(2), S(132), S(40));
        place(profileList, margin + S(14), bodyTop + S(46), leftW - S(28), bodyH - S(118));

        const int fieldX = rightX + S(154);
        const int fieldW = rightW - S(174);
        const int rowH = S(34);
        const int gap = S(10);
        int y = bodyTop + S(54);
        place(name, fieldX, y, fieldW, rowH); y += rowH + gap;
        place(serviceType, fieldX, y, fieldW, S(180)); y += rowH + gap;
        place(apiUrl, fieldX, y, fieldW, rowH); y += rowH + gap;
        place(apiKey, fieldX, y, std::max(S(120), fieldW - S(78)), rowH);
        place(revealButton, fieldX + fieldW - S(72), y, S(32), rowH);
        place(copyButton, fieldX + fieldW - S(36), y, S(32), rowH); y += rowH + S(20);
        place(model, fieldX, y, fieldW, rowH); y += rowH + gap;
        place(timeout, fieldX, y, std::max(S(100), fieldW / 2), rowH); y += rowH + gap;
        place(temperature, fieldX, y, std::max(S(100), fieldW / 2), rowH); y += rowH + gap;
        place(defaultCheck, fieldX, y, fieldW, rowH); y += rowH + gap;
        place(streamingCheck, fieldX, y, S(116), rowH);
        place(toolsCheck, fieldX + S(122), y, S(116), rowH);
        place(retries, fieldX + S(244), y, std::max(S(70), fieldW - S(244)), rowH);

        const int actionY = bodyTop + bodyH - S(86);
        place(testButton, rightX + S(18), actionY, S(126), S(40));
        place(saveButton, rightX + S(154), actionY, S(150), S(40));
        place(deleteButton, rightX + rightW - S(132), actionY, S(114), S(40));

        RedrawWindow(panel, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
    }

    void DrawProfileItem(const DRAWITEMSTRUCT& draw) {
        if (draw.itemID == static_cast<UINT>(-1) || draw.itemData >= profiles.size()) return;
        HDC dc = draw.hDC;
        RECT rect = draw.rcItem;
        InflateRect(&rect, -S(2), -S(4));
        const auto& profile = profiles[draw.itemData];
        const bool selectedItem = (draw.itemState & ODS_SELECTED) != 0;
        const bool configured = ProfileConfigured(profile);
        const COLORREF background = selectedItem ? RGB(239, 246, 255) : RGB(252, 253, 255);
        RoundFill(dc, rect, S(14), background, selectedItem ? RGB(44, 116, 255) : RGB(225, 232, 242));

        HBRUSH dot = CreateSolidBrush(profile.lastMessage.empty() ? RGB(160, 174, 198) :
                                      (profile.lastOk ? RGB(25, 190, 105) : RGB(235, 66, 66)));
        HGDIOBJ oldBrush = SelectObject(dc, dot);
        HPEN noPen = CreatePen(PS_NULL, 0, RGB(0, 0, 0));
        HGDIOBJ oldPen = SelectObject(dc, noPen);
        Ellipse(dc, rect.left + S(13), rect.top + S(23), rect.left + S(23), rect.top + S(33));
        SelectObject(dc, oldPen);
        SelectObject(dc, oldBrush);
        DeleteObject(noPen);
        DeleteObject(dot);

        RECT titleRect{rect.left + S(32), rect.top + S(9), rect.right - S(92), rect.top + S(34)};
        DrawTextSimple(dc, headingFont, RGB(24, 47, 86), profile.name, titleRect);
        std::wstring subtitle;
        if (profile.isDefault) subtitle = L"默认对话服务";
        else if (!configured) subtitle = profile.lastMessage.empty() ? L"等待配置" : profile.lastMessage;
        else subtitle = profile.baseUrl;
        RECT subRect{rect.left + S(32), rect.top + S(34), rect.right - S(24), rect.bottom - S(7)};
        DrawTextSimple(dc, smallFont, RGB(92, 112, 145), subtitle, subRect);

        const std::wstring badge = configured ? L"已配置" : (profile.lastOk ? L"已连接" : L"未配置");
        RECT badgeRect{rect.right - S(82), rect.top + S(17), rect.right - S(17), rect.top + S(42)};
        RoundFill(dc, badgeRect, S(8), configured ? RGB(229, 249, 238) : RGB(255, 246, 228));
        DrawTextSimple(dc, smallFont, configured ? RGB(28, 160, 92) : RGB(218, 137, 33), badge, badgeRect,
                       DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    void Paint(HDC dc) {
        RECT client{};
        GetClientRect(panel, &client);
        FillSolid(dc, client, RGB(246, 250, 255));
        const int width = client.right;
        const int height = client.bottom;
        const int margin = std::max(S(16), width / 40);
        const int contentW = std::max(S(500), width - margin * 2);
        const int headerH = S(72);
        const int statsH = S(82);
        const int statsGap = S(12);
        const int bodyTop = margin + headerH + statsH + S(22);
        const int bodyH = std::max(S(360), height - bodyTop - margin);
        const int columnGap = S(14);
        const int leftW = std::clamp(contentW * 39 / 100, S(280), S(430));
        const int rightX = margin + leftW + columnGap;
        const int rightW = std::max(S(320), contentW - leftW - columnGap);

        RECT title{margin, margin - S(2), margin + contentW - S(150), margin + S(34)};
        DrawTextSimple(dc, titleFont, RGB(18, 39, 75), L"API 配置中心", title);
        RECT subtitle{margin, margin + S(34), margin + contentW - S(150), margin + S(58)};
        DrawTextSimple(dc, smallFont, RGB(91, 110, 142), L"统一管理模型服务、密钥与连接状态", subtitle);

        int configured = 0;
        int unconfigured = 0;
        int defaults = 0;
        for (const auto& profile : profiles) {
            if (ProfileConfigured(profile)) ++configured; else ++unconfigured;
            if (profile.isDefault) ++defaults;
        }
        const int statW = (contentW - statsGap * 2) / 3;
        const std::array<std::pair<std::wstring, int>, 3> stats{{
            {L"已配置", configured}, {L"未配置", unconfigured}, {L"默认服务", defaults}}};
        for (int i = 0; i < 3; ++i) {
            RECT card{margin + i * (statW + statsGap), margin + headerH,
                      margin + i * (statW + statsGap) + statW, margin + headerH + statsH};
            RoundFill(dc, card, S(16), RGB(252, 254, 255), RGB(229, 236, 246));
            const COLORREF iconColor = i == 0 ? RGB(38, 201, 147) : (i == 1 ? RGB(255, 177, 45) : RGB(78, 125, 255));
            RECT icon{card.left + S(16), card.top + S(18), card.left + S(58), card.top + S(60)};
            RoundFill(dc, icon, S(12), iconColor);
            RECT label{card.left + S(72), card.top + S(15), card.right - S(12), card.top + S(37)};
            DrawTextSimple(dc, smallFont, RGB(88, 107, 140), stats[i].first, label);
            RECT number{card.left + S(72), card.top + S(34), card.right - S(12), card.bottom - S(8)};
            DrawTextSimple(dc, metricFont, RGB(19, 44, 83), std::to_wstring(stats[i].second), number);
        }

        RECT leftPanel{margin, bodyTop, margin + leftW, bodyTop + bodyH};
        RECT rightPanel{rightX, bodyTop, rightX + rightW, bodyTop + bodyH};
        RoundFill(dc, leftPanel, S(18), RGB(253, 254, 255), RGB(231, 237, 247));
        RoundFill(dc, rightPanel, S(18), RGB(253, 254, 255), RGB(231, 237, 247));
        RECT leftHeading{leftPanel.left + S(16), leftPanel.top + S(12), leftPanel.right - S(16), leftPanel.top + S(40)};
        DrawTextSimple(dc, headingFont, RGB(28, 49, 83), L"配置列表", leftHeading);
        RECT rightHeading{rightPanel.left + S(18), rightPanel.top + S(12), rightPanel.right - S(18), rightPanel.top + S(40)};
        DrawTextSimple(dc, headingFont, RGB(28, 49, 83), L"配置详情", rightHeading);

        const int fieldX = rightX + S(154);
        int y = bodyTop + S(54);
        const std::array<std::wstring, 8> labels{{L"配置名称", L"服务类型", L"Base URL", L"API Key", L"默认模型", L"超时时间", L"请求温度", L"设为默认服务"}};
        for (int i = 0; i < 8; ++i) {
            RECT label{rightX + S(18), y, fieldX - S(12), y + S(34)};
            DrawTextSimple(dc, bodyFont, RGB(68, 88, 124), labels[i], label);
            y += S(44);
            if (i == 3) y += S(10);
        }
        RECT keyHelp{fieldX, bodyTop + S(54) + S(44) * 4 - S(7), rightX + rightW - S(18),
                     bodyTop + S(54) + S(44) * 4 + S(14)};
        DrawTextSimple(dc, smallFont, RGB(102, 119, 148), L"API Key 已安全保存到 Windows Credential Manager", keyHelp);
        RECT advanced{rightX + S(18), y + S(3), rightX + rightW - S(18), y + S(30)};
        DrawTextSimple(dc, bodyFont, RGB(68, 88, 124), L"高级选项", advanced);

        const int actionY = bodyTop + bodyH - S(86);
        RECT status{rightX + S(18), actionY + S(48), rightX + rightW - S(18), actionY + S(78)};
        RoundFill(dc, status, S(10), statusOk ? RGB(239, 251, 244) : RGB(255, 241, 242));
        DrawTextSimple(dc, smallFont, statusOk ? RGB(34, 161, 96) : RGB(210, 62, 68),
                       std::wstring(L"●  ") + statusMessage, status);

        RECT hint{leftPanel.left + S(16), leftPanel.bottom - S(56), leftPanel.right - S(16), leftPanel.bottom - S(16)};
        RoundFill(dc, hint, S(10), RGB(243, 248, 255));
        DrawTextSimple(dc, smallFont, RGB(91, 112, 147), L"未配置的服务不会出现在可选模型列表中", hint);
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
            if (id == kTestId) { state->TestConnection(); return 0; }
            if (id == kSaveId) { state->PersistForm(false); return 0; }
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
        if (state->panel && IsWindowVisible(state->panel)) {
            SetWindowPos(state->panel, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
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
                               WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                               0, 0, 10, 10, state.panel, ControlId(id), wc.hInstance, nullptr);
    };
    auto check = [&](const wchar_t* text, int id) {
        return CreateWindowExW(0, L"BUTTON", text,
                               WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
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
    state.timeout = edit(kTimeoutId, ES_NUMBER);
    state.temperature = edit(kTemperatureId);
    state.defaultCheck = check(L"启用", kDefaultId);
    state.streamingCheck = check(L"流式输出", kStreamingId);
    state.toolsCheck = check(L"工具调用", kToolsId);
    state.retries = edit(kRetriesId, ES_NUMBER);
    state.addButton = button(L"＋ 新增配置", kNewId);
    state.testButton = button(L"测试连接", kTestId);
    state.saveButton = button(L"保存配置", kSaveId);
    state.deleteButton = button(L"删除配置", kDeleteId);
    state.revealButton = button(L"◉", kRevealId);
    state.copyButton = button(L"复制", kCopyId);

    if (!state.profileList || !state.name || !state.serviceType || !state.apiUrl || !state.apiKey ||
        !state.model || !state.timeout || !state.temperature || !state.defaultCheck ||
        !state.streamingCheck || !state.toolsCheck || !state.retries || !state.addButton ||
        !state.testButton || !state.saveButton || !state.deleteButton || !state.revealButton || !state.copyButton)
        return false;

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
    if (auto* state = StateFor(desktopSettingsWindow); state && state->panel) ShowWindow(state->panel, SW_HIDE);
}

bool DesktopAiSettingsPageVisible(HWND desktopSettingsWindow) {
    const auto* state = StateFor(desktopSettingsWindow);
    return state && state->panel && IsWindowVisible(state->panel);
}

} // namespace turingdesk::wallpaper
