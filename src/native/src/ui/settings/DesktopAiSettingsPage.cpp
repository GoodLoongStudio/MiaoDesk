#include "turingdesk/DesktopAiSettingsPage.h"
#include "turingdesk/L3Agent.h"

#include <commctrl.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace turingdesk::wallpaper {
namespace {

constexpr wchar_t kPageClass[] = L"TuringDesk.Native.DesktopAiSettingsPage";
constexpr wchar_t kStateProperty[] = L"TuringDesk.DesktopAiSettingsPage.State";
constexpr UINT_PTR kParentSubclassId = 0x54444149; // "TDAI"
constexpr int kFirstNavId = 6110;
constexpr int kAiNavId = 6116;
constexpr int kProfileId = 7300;
constexpr int kApiUrlId = 7301;
constexpr int kApiKeyId = 7302;
constexpr int kModelId = 7303;
constexpr int kSaveApiId = 7304;
constexpr int kOpenHarnessId = 7305;
constexpr wchar_t kStoredKeyMask[] = L"********";

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

fs::path ModuleDirectory() {
    wchar_t path[32768]{};
    const DWORD length = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
    if (length == 0 || length >= std::size(path)) return {};
    return fs::path(std::wstring(path, length)).parent_path();
}

struct PageState {
    HWND parent{};
    HWND panel{};
    HWND title{};
    HWND intro{};
    HWND profileLabel{};
    HWND profileCombo{};
    HWND provider{};
    HWND apiLabel{};
    HWND apiUrl{};
    HWND keyLabel{};
    HWND apiKey{};
    HWND modelLabel{};
    HWND model{};
    HWND save{};
    HWND status{};
    HWND harnessTitle{};
    HWND harnessText{};
    HWND harnessOpen{};
    HFONT titleFont{};
    HFONT sectionFont{};
    HFONT bodyFont{};
    HFONT smallFont{};
    HBRUSH whiteBrush{};
    bool hasSavedProfile{};
    int scrollY{};
    int contentHeight{};
    L3Agent agent;

    ~PageState() {
        if (panel && IsWindow(panel)) DestroyWindow(panel);
        for (HFONT font : {titleFont, sectionFont, bodyFont, smallFont}) {
            if (font) DeleteObject(font);
        }
        if (whiteBrush) DeleteObject(whiteBrush);
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
        for (HFONT* font : {&titleFont, &sectionFont, &bodyFont, &smallFont}) {
            if (*font) { DeleteObject(*font); *font = nullptr; }
        }
        titleFont = MakeFont(22, FW_SEMIBOLD);
        sectionFont = MakeFont(16, FW_SEMIBOLD);
        bodyFont = MakeFont(13, FW_NORMAL);
        smallFont = MakeFont(11, FW_NORMAL);
        for (HWND control : {title, intro, profileLabel, profileCombo, provider, apiLabel, apiUrl, keyLabel,
                             apiKey, modelLabel, model, save, status, harnessTitle, harnessText, harnessOpen}) {
            if (control) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(bodyFont), TRUE);
        }
        if (title) SendMessageW(title, WM_SETFONT, reinterpret_cast<WPARAM>(titleFont), TRUE);
        if (provider) SendMessageW(provider, WM_SETFONT, reinterpret_cast<WPARAM>(smallFont), TRUE);
        if (status) SendMessageW(status, WM_SETFONT, reinterpret_cast<WPARAM>(smallFont), TRUE);
        if (harnessTitle) SendMessageW(harnessTitle, WM_SETFONT, reinterpret_cast<WPARAM>(sectionFont), TRUE);
    }

    std::wstring CurrentApi() const {
        std::wstring api = agent.CurrentApiUrl();
        if (api.empty()) api = agent.Config().baseUrl;
        return api;
    }

    void RefreshProfileOptions() {
        if (!profileCombo) return;
        const auto& config = agent.Config();
        const std::wstring api = CurrentApi();
        hasSavedProfile = !api.empty() || !config.model.empty() || agent.HasStoredApiKey();

        SendMessageW(profileCombo, CB_RESETCONTENT, 0, 0);
        if (hasSavedProfile) {
            std::wstring label;
            if (!config.providerId.empty() && config.providerId != L"unconfigured") label = config.providerId;
            else label = L"当前 API";
            if (!config.model.empty()) label += L" · " + config.model;
            SendMessageW(profileCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
            SendMessageW(profileCombo, CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(L"＋ 新配置 / 替换当前配置"));
            SendMessageW(profileCombo, CB_SETCURSEL, 0, 0);
        } else {
            SendMessageW(profileCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"＋ 新建模型 API 配置"));
            SendMessageW(profileCombo, CB_SETCURSEL, 0, 0);
        }
    }

    void RefreshConfigText() {
        const auto& config = agent.Config();
        std::wstring providerText = L"当前 Provider：";
        providerText += config.providerId.empty() || config.providerId == L"unconfigured" ? L"未配置" : config.providerId;
        providerText += agent.HasStoredApiKey() ? L"  ·  API Key 已安全保存" : L"  ·  尚未保存 API Key";
        SetWindowTextW(provider, providerText.c_str());

        SetWindowTextW(apiUrl, CurrentApi().c_str());
        SetWindowTextW(model, config.model.c_str());
        SetWindowTextW(apiKey, agent.HasStoredApiKey() ? kStoredKeyMask : L"");
        SendMessageW(apiKey, EM_SETCUEBANNER, TRUE,
                     reinterpret_cast<LPARAM>(agent.HasStoredApiKey()
                         ? L"已保存密钥；留空保持现有密钥"
                         : L"粘贴 API Key"));
        RefreshProfileOptions();
    }

    void SelectCustomProfile() {
        SetWindowTextW(apiUrl, L"");
        SetWindowTextW(apiKey, L"");
        SetWindowTextW(model, L"");
        SetWindowTextW(provider, L"新配置：填写 API 地址、API Key 与 Model 后检测并保存。");
        SendMessageW(apiKey, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"粘贴 API Key"));
        SetStatus(L"新配置保存后会成为 Pi Agent 与 Direct Model 的当前默认配置。");
        SetFocus(apiUrl);
    }

    void OnProfileSelectionChanged() {
        const LRESULT selected = SendMessageW(profileCombo, CB_GETCURSEL, 0, 0);
        const bool custom = !hasSavedProfile || selected > 0;
        if (custom) SelectCustomProfile();
        else RefreshConfigText();
    }

    void SetStatus(std::wstring text) {
        if (!status) return;
        SetWindowTextW(status, text.c_str());
        if (panel && IsWindow(panel)) Layout();
    }

    void SaveApi() {
        const std::wstring api = WindowText(apiUrl);
        std::wstring key = WindowText(apiKey);
        if (key == kStoredKeyMask) key.clear();
        std::wstring chosenModel = WindowText(model);
        const LRESULT selected = profileCombo ? SendMessageW(profileCombo, CB_GETCURSEL, 0, 0) : 0;
        const bool customProfile = !hasSavedProfile || selected > 0;

        if (api.empty()) {
            SetStatus(L"请输入 API 地址，例如 https://api.deepseek.com/v1。");
            SetFocus(apiUrl);
            return;
        }
        if (customProfile && key.empty()) {
            SetStatus(L"新配置需要填写 API Key；已有配置的密钥不会自动复制到新配置。");
            SetFocus(apiKey);
            return;
        }

        EnableWindow(save, FALSE);
        SetStatus(L"正在检测 Provider 和可用模型…");
        UpdateWindow(status);
        HCURSOR previous = SetCursor(LoadCursorW(nullptr, IDC_WAIT));

        const ModelProbeResult probe = agent.ProbeModels(api, key, true);
        if (!probe.ok) {
            SetStatus(probe.message.empty() ? L"API 检测失败，请检查地址、密钥和网络。" : probe.message);
            EnableWindow(save, TRUE);
            SetCursor(previous);
            return;
        }

        if (chosenModel.empty()) chosenModel = probe.recommendedModel;
        if (chosenModel.empty() && !probe.models.empty()) chosenModel = probe.models.front();
        if (chosenModel.empty()) {
            SetStatus(L"API 已连接，但没有找到可用模型。请手动填写 Model。");
            EnableWindow(save, TRUE);
            SetCursor(previous);
            SetFocus(model);
            return;
        }

        std::wstring reply;
        const bool preserveExistingKey = !customProfile && key.empty();
        if (!agent.ApplyModelConfig(probe, chosenModel, key, preserveExistingKey, reply)) {
            SetStatus(reply.empty() ? L"保存 AI 配置失败。" : reply);
            EnableWindow(save, TRUE);
            SetCursor(previous);
            return;
        }

        RefreshConfigText();
        SetStatus(reply.empty()
            ? L"已保存。Pi Agent 与 Direct Model 将共用这套模型配置。"
            : reply);
        EnableWindow(save, TRUE);
        SetCursor(previous);
    }

    void OpenHarness() {
        const fs::path directory = ModuleDirectory();
        const fs::path executable = directory / L"TuringDeskHarness.exe";
        std::error_code ec;
        if (directory.empty() || !fs::is_regular_file(executable, ec)) {
            SetStatus(L"当前安装/预览包缺少 DeepSeek Harness 组件，请更新到最新版本。");
            return;
        }
        const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(
            panel, L"open", executable.c_str(), L"--ui", directory.c_str(), SW_SHOWNORMAL));
        if (result <= 32) {
            SetStatus(L"DeepSeek Harness 启动失败。请检查 RuntimeBundle 或 harness.log。");
            return;
        }
        SetStatus(L"正在打开 DeepSeek Harness 工作台…");
    }

    int MeasureTextHeight(HWND control, int width, HFONT font, int minimum) const {
        if (!control || width <= 0) return minimum;
        const std::wstring text = WindowText(control);
        if (text.empty()) return minimum;
        HDC dc = GetDC(panel);
        if (!dc) return minimum;
        HGDIOBJ previous = font ? SelectObject(dc, font) : nullptr;
        RECT measure{0, 0, width, 0};
        DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &measure,
                  DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
        if (previous) SelectObject(dc, previous);
        ReleaseDC(panel, dc);
        return std::max(minimum, static_cast<int>(measure.bottom - measure.top) + S(3));
    }

    void SetScrollPosition(int requested) {
        if (!panel) return;
        RECT area{};
        GetClientRect(panel, &area);
        const int viewport = std::max(1, static_cast<int>(area.bottom - area.top));
        const int maxScroll = std::max(0, contentHeight - viewport);
        const int next = std::clamp(requested, 0, maxScroll);
        if (next == scrollY) return;
        scrollY = next;
        Layout();
    }

    void Layout() {
        if (!parent || !panel) return;
        RECT client{};
        GetClientRect(parent, &client);
        const int sidebar = S(208);
        const int top = S(58);
        const int parentWidth = std::max(1, static_cast<int>(client.right - client.left));
        const int parentHeight = std::max(1, static_cast<int>(client.bottom - client.top));
        SetWindowPos(panel, nullptr, sidebar, top, std::max(1, parentWidth - sidebar),
                     std::max(1, parentHeight - top),
                     SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOREDRAW);

        RECT area{};
        GetClientRect(panel, &area);
        const int width = std::max(1, static_cast<int>(area.right - area.left));
        const int height = std::max(1, static_cast<int>(area.bottom - area.top));

        // Continuous layout: every dimension scales with the current client size.
        const int margin = std::max(S(12), MulDiv(width, 28, 1000));
        const int contentW = std::max(S(200), width - margin * 2);
        const int labelW = std::max(S(68), MulDiv(contentW, 165, 1000));
        const int fieldGap = std::max(S(8), MulDiv(width, 12, 1000));
        const int fieldX = margin + labelW + fieldGap;
        const int fieldW = std::max(S(120), contentW - labelW - fieldGap);
        const int rowH = MulDiv(height, 34, 1000) + S(28);
        const int rowGap = MulDiv(height, 10, 1000) + S(4);

        const int introH = MeasureTextHeight(intro, contentW, bodyFont, S(24));
        const int providerH = MeasureTextHeight(provider, fieldW, smallFont, S(22));
        const int statusH = MeasureTextHeight(status, contentW, smallFont, S(24));
        const int harnessTextH = MeasureTextHeight(harnessText, contentW, bodyFont, S(24));

        ShowWindow(title, SW_HIDE);
        int y = S(16);
        const int titleY = y; const int titleH = 1;
        const int introY = y; y += introH + S(12);
        const int profileY = y; y += rowH + rowGap;
        const int providerY = y; y += providerH + S(8);
        const int apiY = y; y += rowH + rowGap;
        const int keyY = y; y += rowH + rowGap;
        const int modelY = y; y += rowH + S(12);
        const int saveY = y; y += S(38) + S(10);
        const int statusY = y; y += statusH + S(14);
        const int harnessTitleY = y; y += S(28) + S(5);
        const int harnessTextY = y; y += harnessTextH + S(12);
        const int harnessOpenY = y; y += S(38);
        contentHeight = y + margin;

        const int maxScroll = std::max(0, contentHeight - height);
        scrollY = std::clamp(scrollY, 0, maxScroll);
        SCROLLINFO scroll{};
        scroll.cbSize = sizeof(scroll);
        scroll.fMask = SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL;
        scroll.nMin = 0;
        scroll.nMax = std::max(0, contentHeight - 1);
        scroll.nPage = static_cast<UINT>(height);
        scroll.nPos = scrollY;
        SetScrollInfo(panel, SB_VERT, &scroll, TRUE);

        auto place = [&](HWND control, int x, int top, int w, int h) {
            if (!control) return;
            SetWindowPos(control, nullptr, x, top - scrollY, std::max(1, w), std::max(1, h),
                         SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOREDRAW);
        };

        place(title, margin, titleY, contentW, titleH);
        place(intro, margin, introY, contentW, introH);
        place(profileLabel, margin, profileY + S(5), labelW, S(22));
        place(profileCombo, fieldX, profileY, fieldW, rowH);
        place(provider, fieldX, providerY, fieldW, providerH);
        place(apiLabel, margin, apiY + S(5), labelW, S(22));
        place(apiUrl, fieldX, apiY, fieldW, rowH);
        place(keyLabel, margin, keyY + S(5), labelW, S(22));
        place(apiKey, fieldX, keyY, fieldW, rowH);
        place(modelLabel, margin, modelY + S(5), labelW, S(22));
        place(model, fieldX, modelY, fieldW, rowH);
        place(save, fieldX, saveY, std::min(fieldW, std::max(S(140), MulDiv(fieldW, 50, 100))), S(38));
        place(status, margin, statusY, contentW, statusH);
        place(harnessTitle, margin, harnessTitleY, contentW, S(28));
        place(harnessText, margin, harnessTextY, contentW, harnessTextH);
        place(harnessOpen, margin, harnessOpenY,
              std::min(contentW, std::max(S(160), MulDiv(contentW, 38, 100))), S(38));

        RedrawWindow(panel, nullptr, nullptr,
                     RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
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
    case WM_MOUSEWHEEL: {
        const short delta = static_cast<short>(HIWORD(wParam));
        const int distance = MulDiv(static_cast<int>(delta), state->S(54), WHEEL_DELTA);
        state->SetScrollPosition(state->scrollY - distance);
        return 0;
    }
    case WM_VSCROLL: {
        RECT area{};
        GetClientRect(window, &area);
        const int viewport = std::max(1, static_cast<int>(area.bottom - area.top));
        int next = state->scrollY;
        switch (LOWORD(wParam)) {
        case SB_LINEUP: next -= state->S(34); break;
        case SB_LINEDOWN: next += state->S(34); break;
        case SB_PAGEUP: next -= std::max(state->S(80), viewport * 4 / 5); break;
        case SB_PAGEDOWN: next += std::max(state->S(80), viewport * 4 / 5); break;
        case SB_TOP: next = 0; break;
        case SB_BOTTOM: next = state->contentHeight; break;
        case SB_THUMBPOSITION:
        case SB_THUMBTRACK: {
            SCROLLINFO info{};
            info.cbSize = sizeof(info);
            info.fMask = SIF_TRACKPOS;
            if (GetScrollInfo(window, SB_VERT, &info)) next = info.nTrackPos;
            break;
        }
        default: return 0;
        }
        state->SetScrollPosition(next);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == kProfileId && HIWORD(wParam) == CBN_SELCHANGE) {
            state->OnProfileSelectionChanged();
            return 0;
        }
        if (HIWORD(wParam) == BN_CLICKED && LOWORD(wParam) == kSaveApiId) {
            state->SaveApi();
            return 0;
        }
        if (HIWORD(wParam) == BN_CLICKED && LOWORD(wParam) == kOpenHarnessId) {
            state->OpenHarness();
            return 0;
        }
        break;
    case WM_ERASEBKGND: {
        RECT rect{};
        GetClientRect(window, &rect);
        FillRect(reinterpret_cast<HDC>(wParam), &rect, state->whiteBrush);
        return 1;
    }
    case WM_CTLCOLORSTATIC:
        SetBkColor(reinterpret_cast<HDC>(wParam), RGB(255, 255, 255));
        SetTextColor(reinterpret_cast<HDC>(wParam), RGB(45, 48, 56));
        return reinterpret_cast<LRESULT>(state->whiteBrush);
    case WM_CTLCOLOREDIT:
        SetBkColor(reinterpret_cast<HDC>(wParam), RGB(255, 255, 255));
        SetTextColor(reinterpret_cast<HDC>(wParam), RGB(35, 38, 45));
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
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
            SetWindowPos(state->panel, HWND_TOP, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
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
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

    state.whiteBrush = CreateSolidBrush(RGB(255, 255, 255));
    state.panel = CreateWindowExW(WS_EX_CONTROLPARENT, kPageClass, L"",
                                  WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | WS_VSCROLL,
                                  0, 0, 10, 10, state.parent, nullptr, wc.hInstance, &state);
    if (!state.panel) return false;

    auto label = [&](const wchar_t* text, DWORD style = SS_LEFT) {
        return CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | style,
                               0, 0, 10, 10, state.panel, nullptr, wc.hInstance, nullptr);
    };
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

    state.title = label(L"妙喵 AI");
    state.intro = label(L"配置模型后，妙喵可用完整 Agent 能力。"
                        L"API Key 保存在 Windows Credential Manager。",
                        SS_LEFT | SS_NOPREFIX);
    state.profileLabel = label(L"配置");
    state.profileCombo = CreateWindowExW(WS_EX_CLIENTEDGE, L"COMBOBOX", L"",
                                         WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
                                         0, 0, 10, 200, state.panel, ControlId(kProfileId), wc.hInstance, nullptr);
    state.provider = label(L"");
    state.apiLabel = label(L"API 地址");
    state.apiUrl = edit(kApiUrlId);
    state.keyLabel = label(L"API Key");
    state.apiKey = edit(kApiKeyId, ES_PASSWORD);
    state.modelLabel = label(L"Model");
    state.model = edit(kModelId);
    state.save = button(L"检测并保存配置", kSaveApiId);
    state.status = label(L"保存后对话与桌面 Agent 共用当前配置。",
                         SS_LEFT | SS_NOPREFIX);
    state.harnessTitle = label(L"DeepSeek Harness");
    state.harnessText = label(L"官方 DeepSeek Harness 高级 Agent 工作台，会同步使用上方保存的 API 配置。",
                              SS_LEFT | SS_NOPREFIX);
    state.harnessOpen = button(L"打开 Harness 工作台", kOpenHarnessId);

    if (!state.title || !state.intro || !state.profileLabel || !state.profileCombo || !state.provider ||
        !state.apiLabel || !state.apiUrl || !state.keyLabel || !state.apiKey || !state.modelLabel || !state.model ||
        !state.save || !state.status || !state.harnessTitle || !state.harnessText || !state.harnessOpen) return false;

    SendMessageW(state.apiUrl, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"https://api.deepseek.com/v1"));
    SendMessageW(state.model, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"例如 deepseek-chat"));
    state.RebuildFonts();
    state.RefreshConfigText();
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

    state->RefreshConfigText();
    state->Layout();
    ShowWindow(state->panel, SW_SHOW);
    SetWindowPos(state->panel, HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    InvalidateRect(state->panel, nullptr, TRUE);
    UpdateWindow(state->panel);
    return true;
}

void HideDesktopAiSettingsPage(HWND desktopSettingsWindow) {
    if (auto* state = StateFor(desktopSettingsWindow); state && state->panel) {
        ShowWindow(state->panel, SW_HIDE);
    }
}

bool DesktopAiSettingsPageVisible(HWND desktopSettingsWindow) {
    const auto* state = StateFor(desktopSettingsWindow);
    return state && state->panel && IsWindowVisible(state->panel);
}

} // namespace turingdesk::wallpaper
