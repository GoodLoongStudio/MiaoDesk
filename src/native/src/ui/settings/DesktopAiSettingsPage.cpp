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

namespace fs = std::filesystem;

namespace turingdesk::wallpaper {
namespace {

constexpr wchar_t kPageClass[] = L"TuringDesk.Native.DesktopAiSettingsPage";
constexpr wchar_t kStateProperty[] = L"TuringDesk.DesktopAiSettingsPage.State";
constexpr UINT_PTR kParentSubclassId = 0x54444149; // "TDAI"
constexpr int kWallpaperNavId = 6110;
constexpr int kWidgetsNavId = 6111;
constexpr int kApiUrlId = 7301;
constexpr int kApiKeyId = 7302;
constexpr int kModelId = 7303;
constexpr int kSaveApiId = 7304;
constexpr int kOpenHarnessId = 7305;

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
        bodyFont = MakeFont(14, FW_NORMAL);
        smallFont = MakeFont(12, FW_NORMAL);
        for (HWND control : {title, intro, provider, apiLabel, apiUrl, keyLabel, apiKey, modelLabel, model,
                             save, status, harnessTitle, harnessText, harnessOpen}) {
            if (control) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(bodyFont), TRUE);
        }
        if (title) SendMessageW(title, WM_SETFONT, reinterpret_cast<WPARAM>(titleFont), TRUE);
        if (provider) SendMessageW(provider, WM_SETFONT, reinterpret_cast<WPARAM>(smallFont), TRUE);
        if (status) SendMessageW(status, WM_SETFONT, reinterpret_cast<WPARAM>(smallFont), TRUE);
        if (harnessTitle) SendMessageW(harnessTitle, WM_SETFONT, reinterpret_cast<WPARAM>(sectionFont), TRUE);
    }

    void RefreshConfigText() {
        const auto& config = agent.Config();
        std::wstring providerText = L"当前 Provider：";
        providerText += config.providerId.empty() || config.providerId == L"unconfigured" ? L"未配置" : config.providerId;
        providerText += agent.HasStoredApiKey() ? L"  ·  API Key 已安全保存" : L"  ·  尚未保存 API Key";
        SetWindowTextW(provider, providerText.c_str());

        std::wstring api = agent.CurrentApiUrl();
        if (api.empty()) api = config.baseUrl;
        SetWindowTextW(apiUrl, api.c_str());
        SetWindowTextW(model, config.model.c_str());
        SetWindowTextW(apiKey, L"");
        SendMessageW(apiKey, EM_SETCUEBANNER, TRUE,
                     reinterpret_cast<LPARAM>(agent.HasStoredApiKey()
                         ? L"已保存密钥；留空保持现有密钥"
                         : L"粘贴 API Key"));
    }

    void SetStatus(std::wstring text) {
        if (status) SetWindowTextW(status, text.c_str());
    }

    void SaveApi() {
        const std::wstring api = WindowText(apiUrl);
        const std::wstring key = WindowText(apiKey);
        std::wstring chosenModel = WindowText(model);
        if (api.empty()) {
            SetStatus(L"请输入 API 地址，例如 https://api.deepseek.com/v1。 ");
            SetFocus(apiUrl);
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
            SetStatus(L"API 已连接，但没有找到可用模型。请手动填写 Model。 ");
            EnableWindow(save, TRUE);
            SetCursor(previous);
            SetFocus(model);
            return;
        }

        std::wstring reply;
        const bool preserveExistingKey = key.empty();
        if (!agent.ApplyModelConfig(probe, chosenModel, key, preserveExistingKey, reply)) {
            SetStatus(reply.empty() ? L"保存 AI 配置失败。" : reply);
            EnableWindow(save, TRUE);
            SetCursor(previous);
            return;
        }

        SetWindowTextW(model, chosenModel.c_str());
        SetWindowTextW(apiKey, L"");
        RefreshConfigText();
        SetStatus(reply.empty()
            ? L"已保存。Pi Agent 与 Direct Model fallback 将共用这套 Provider / Model / API Key。"
            : reply);
        EnableWindow(save, TRUE);
        SetCursor(previous);
    }

    void OpenHarness() {
        const fs::path directory = ModuleDirectory();
        const fs::path executable = directory / L"TuringDeskHarness.exe";
        std::error_code ec;
        if (directory.empty() || !fs::is_regular_file(executable, ec)) {
            SetStatus(L"当前安装/预览包缺少 TuringDeskHarness.exe，请更新到最新版本。 ");
            return;
        }
        const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(
            panel, L"open", executable.c_str(), L"--ui", directory.c_str(), SW_SHOWNORMAL));
        if (result <= 32) {
            SetStatus(L"DeepSeek Harness 启动失败。请检查 RuntimeBundle 或 Harness 日志。 ");
            return;
        }
        SetStatus(L"正在打开 DeepSeek Harness…");
    }

    void Layout() {
        if (!parent || !panel) return;
        RECT client{};
        GetClientRect(parent, &client);
        const int sidebar = S(208);
        const int parentWidth = std::max(1, static_cast<int>(client.right - client.left));
        const int parentHeight = std::max(1, static_cast<int>(client.bottom - client.top));
        SetWindowPos(panel, HWND_TOP, sidebar, 0, std::max(1, parentWidth - sidebar), parentHeight,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);

        RECT area{};
        GetClientRect(panel, &area);
        const int width = std::max(1, static_cast<int>(area.right - area.left));
        const int margin = S(28);
        const int labelW = S(88);
        const int rowH = S(34);
        const int gap = S(12);
        const int contentW = std::max(S(260), std::min(S(720), width - margin * 2));
        const int fieldX = margin + labelW;
        const int fieldW = std::max(S(180), contentW - labelW);

        int y = S(22);
        MoveWindow(title, margin, y, contentW, S(34), TRUE); y += S(45);
        MoveWindow(intro, margin, y, contentW, S(44), TRUE); y += S(50);
        MoveWindow(provider, margin, y, contentW, S(24), TRUE); y += S(38);

        MoveWindow(apiLabel, margin, y + S(7), labelW - gap, S(22), TRUE);
        MoveWindow(apiUrl, fieldX, y, fieldW, rowH, TRUE); y += rowH + gap;
        MoveWindow(keyLabel, margin, y + S(7), labelW - gap, S(22), TRUE);
        MoveWindow(apiKey, fieldX, y, fieldW, rowH, TRUE); y += rowH + gap;
        MoveWindow(modelLabel, margin, y + S(7), labelW - gap, S(22), TRUE);
        MoveWindow(model, fieldX, y, fieldW, rowH, TRUE); y += rowH + S(14);
        MoveWindow(save, fieldX, y, S(150), S(38), TRUE); y += S(50);
        MoveWindow(status, margin, y, contentW, S(48), TRUE); y += S(70);

        MoveWindow(harnessTitle, margin, y, contentW, S(30), TRUE); y += S(36);
        MoveWindow(harnessText, margin, y, contentW, S(52), TRUE); y += S(62);
        MoveWindow(harnessOpen, margin, y, S(190), S(40), TRUE);
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
    case WM_COMMAND:
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
        if (id == kWallpaperNavId || id == kWidgetsNavId) HideDesktopAiSettingsPage(parent);
    }

    const LRESULT result = DefSubclassProc(parent, message, wParam, lParam);
    auto* state = StateFor(parent);
    if (state && (message == WM_SIZE || message == WM_DPICHANGED || message == WM_SHOWWINDOW)) {
        state->Layout();
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
                                  WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
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

    state.title = label(L"图灵 AI");
    state.intro = label(L"配置图灵 AI 使用的模型 API。Provider、Model 与 API Key 同时供 Pi Agent 和 Direct Model fallback 使用。",
                        SS_LEFT | SS_NOPREFIX);
    state.provider = label(L"");
    state.apiLabel = label(L"API 地址");
    state.apiUrl = edit(kApiUrlId);
    state.keyLabel = label(L"API Key");
    state.apiKey = edit(kApiKeyId, ES_PASSWORD);
    state.modelLabel = label(L"Model");
    state.model = edit(kModelId);
    state.save = button(L"检测并保存 API", kSaveApiId);
    state.status = label(L"API Key 使用 Windows Credential Manager 保存，不会在这里回显明文。",
                         SS_LEFT | SS_NOPREFIX);
    state.harnessTitle = label(L"DeepSeek Harness");
    state.harnessText = label(L"打开随 TuringDesk 部署的 DeepSeek Harness 管理界面。Harness 使用本机 loopback 服务，不会自动下载 npm/npx 依赖。",
                              SS_LEFT | SS_NOPREFIX);
    state.harnessOpen = button(L"打开 DeepSeek Harness", kOpenHarnessId);

    if (!state.title || !state.intro || !state.provider || !state.apiUrl || !state.apiKey || !state.model ||
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
    SetWindowPos(state->panel, HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(state->panel, nullptr, TRUE);
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

} // namespace turingdesk::wallpaper
