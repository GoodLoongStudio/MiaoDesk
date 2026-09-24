#include "miaodesk/ContentSkillBrowserDialog.h"

#include "miaodesk/ContentCreatorBridge.h"
#include "miaodesk/NativeTools.h"
#include "miaodesk/NativeUiScale.h"

#include <algorithm>
#include <array>
#include <string>

namespace miaodesk::wallpaper {
namespace {

constexpr wchar_t kWindowClass[] = L"MiaoDesk.Native.ContentSkillBrowser";
constexpr int kListId = 7601;
constexpr int kAiId = 7602;
constexpr int kCloseId = 7603;

HMENU ControlId(int id) {
    return reinterpret_cast<HMENU>(static_cast<INT_PTR>(id));
}

struct SkillItem {
    const wchar_t* label;
    const char* name;
};

constexpr std::array<SkillItem, 3> kWallpaperSkills{{
    {L"1. 内容包基础 · content-package-basics", "content-package-basics"},
    {L"2. 壁纸规则 · wallpaper-content", "wallpaper-content"},
    {L"3. 交付检查 · content-review", "content-review"},
}};

constexpr std::array<SkillItem, 3> kWidgetSkills{{
    {L"1. 内容包基础 · content-package-basics", "content-package-basics"},
    {L"2. 组件规则 · widget-content", "widget-content"},
    {L"3. 交付检查 · content-review", "content-review"},
}};

struct DialogState {
    HINSTANCE instance{};
    HWND owner{};
    ContentSkillBrowserDomain domain{ContentSkillBrowserDomain::Wallpaper};
    HWND window{};
    HWND heading{};
    HWND note{};
    HWND list{};
    HWND content{};
    HWND aiButton{};
    HWND closeButton{};
    HFONT font{};
    HFONT headingFont{};
    UINT fontScaleDpi{};

    int S(int value) const noexcept {
        const UINT dpi = window ? std::max<UINT>(USER_DEFAULT_SCREEN_DPI, GetDpiForWindow(window))
                                : USER_DEFAULT_SCREEN_DPI;
        return MulDiv(value, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
    }

    const std::array<SkillItem, 3>& Skills() const noexcept {
        return domain == ContentSkillBrowserDomain::Widget ? kWidgetSkills : kWallpaperSkills;
    }

    creator::ContentCreatorKind CreatorKind() const noexcept {
        return domain == ContentSkillBrowserDomain::Widget
            ? creator::ContentCreatorKind::Widget
            : creator::ContentCreatorKind::Wallpaper;
    }

    void DestroyFonts() noexcept {
        if (headingFont) { DeleteObject(headingFont); headingFont = nullptr; }
        if (font) { DeleteObject(font); font = nullptr; }
    }

    void RebuildFonts() {
        fontScaleDpi = ui::EffectiveFontDpi(window);
        DestroyFonts();
        font = ui::CreateUiFont(window, 14, FW_NORMAL);
        headingFont = ui::CreateUiFont(window, 17, FW_SEMIBOLD);
    }

    void ApplyFonts() const {
        if (heading && headingFont)
            SendMessageW(heading, WM_SETFONT, reinterpret_cast<WPARAM>(headingFont), TRUE);
        for (HWND child : {note, list, content, aiButton, closeButton})
            if (child && font)
                SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        if (list)
            SendMessageW(list, LB_SETITEMHEIGHT, 0,
                         static_cast<LPARAM>(std::max(S(24), ui::ScaleFontPx(window, 22))));
    }

    void RefreshFontScaleIfNeeded() {
        if (!window || !font) return;
        if (ui::EffectiveFontDpi(window) == fontScaleDpi) return;
        RebuildFonts();
        ApplyFonts();
        Layout();
    }

    void Layout() const {
        if (!window) return;
        RECT rc{};
        GetClientRect(window, &rc);
        const int w = std::max(1, static_cast<int>(rc.right - rc.left));
        const int h = std::max(1, static_cast<int>(rc.bottom - rc.top));
        const int margin = S(16);
        const int gap = S(10);
        const int titleH = S(30);
        const int noteH = S(42);
        const int footerH = S(42);
        const int listW = std::clamp(w * 31 / 100, S(250), S(360));

        auto place = [&](HWND child, int x, int y, int width, int height) {
            if (!child) return;
            SetWindowPos(child, nullptr, x, y, std::max(1, width), std::max(1, height),
                         SWP_NOZORDER | SWP_NOACTIVATE);
        };

        place(heading, margin, margin, w - margin * 2, titleH);
        place(note, margin, margin + titleH + S(4), w - margin * 2, noteH);

        const int bodyTop = margin + titleH + S(4) + noteH + gap;
        const int footerTop = h - margin - footerH;
        const int bodyH = std::max(S(120), footerTop - gap - bodyTop);
        place(list, margin, bodyTop, listW, bodyH);
        place(content, margin + listW + gap, bodyTop,
              w - margin * 2 - listW - gap, bodyH);

        const int aiW = S(170);
        const int closeW = S(90);
        place(aiButton, margin, footerTop, aiW, footerH);
        place(closeButton, w - margin - closeW, footerTop, closeW, footerH);
    }

    void LoadSelected() const {
        if (!list || !content) return;
        const LRESULT selected = SendMessageW(list, LB_GETCURSEL, 0, 0);
        if (selected == LB_ERR || selected < 0 ||
            static_cast<std::size_t>(selected) >= Skills().size())
            return;
        const auto& skill = Skills()[static_cast<std::size_t>(selected)];
        const std::string args = std::string("{\"name\":\"") + skill.name + "\"}";
        const auto result = ExecuteNativeToolRaw("content_skill_get", args);
        const std::wstring text = result.success
            ? result.message
            : (L"无法读取 Skill：\r\n" + result.message);
        SetWindowTextW(content, text.c_str());
        SendMessageW(content, EM_SETSEL, 0, 0);
        SendMessageW(content, EM_SCROLLCARET, 0, 0);
    }

    bool CreateControls() {
        RebuildFonts();
        heading = CreateWindowExW(
            0, L"STATIC",
            domain == ContentSkillBrowserDomain::Widget
                ? L"小组件创作 Skills"
                : L"壁纸创作 Skills",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            0, 0, 10, 10, window, nullptr, instance, nullptr);
        note = CreateWindowExW(
            0, L"STATIC",
            L"这里展示的就是 AI 通过 content_skill_get 读取的同一份规范。"
            L"修改 Skill 文件后，人和 AI 会同时看到新版本。",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            0, 0, 10, 10, window, nullptr, instance, nullptr);
        list = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
                LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
            0, 0, 10, 10, window, ControlId(kListId), instance, nullptr);
        content = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
                ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | ES_NOHIDESEL,
            0, 0, 10, 10, window, nullptr, instance, nullptr);
        aiButton = CreateWindowExW(
            0, L"BUTTON",
            domain == ContentSkillBrowserDomain::Widget
                ? L"✨ 用 AI 制作组件"
                : L"✨ 用 AI 制作壁纸",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            0, 0, 10, 10, window, ControlId(kAiId), instance, nullptr);
        closeButton = CreateWindowExW(
            0, L"BUTTON", L"关闭",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            0, 0, 10, 10, window, ControlId(kCloseId), instance, nullptr);

        if (!heading || !note || !list || !content || !aiButton || !closeButton)
            return false;

        for (const auto& item : Skills())
            SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item.label));
        SendMessageW(list, LB_SETCURSEL, 0, 0);
        ApplyFonts();
        LoadSelected();
        return true;
    }

    void OpenAi() const {
        std::wstring error;
        if (!creator::OpenConversation(CreatorKind(), window, &error)) {
            MessageBoxW(window,
                        error.empty() ? L"无法打开 AI 创作窗口。" : error.c_str(),
                        L"妙喵 AI", MB_OK | MB_ICONERROR);
        }
    }
};

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<DialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = static_cast<DialogState*>(create->lpCreateParams);
        if (!state) return FALSE;
        state->window = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    if (!state) return DefWindowProcW(hwnd, message, wParam, lParam);

    switch (message) {
    case WM_CREATE:
        if (!state->CreateControls()) return -1;
        state->Layout();
        return 0;
    case WM_MOVE:
    case WM_DISPLAYCHANGE:
        state->RefreshFontScaleIfNeeded();
        return 0;
    case WM_SIZE:
        state->Layout();
        return 0;
    case WM_DPICHANGED: {
        const auto* suggested = reinterpret_cast<const RECT*>(lParam);
        if (suggested) {
            SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left,
                         suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        state->RebuildFonts();
        state->ApplyFonts();
        state->Layout();
        return 0;
    }
    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        const int notification = HIWORD(wParam);
        if (id == kListId && notification == LBN_SELCHANGE) {
            state->LoadSelected();
            return 0;
        }
        if (id == kAiId && notification == BN_CLICKED) {
            state->OpenAi();
            return 0;
        }
        if (id == kCloseId && notification == BN_CLICKED) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_NCDESTROY:
        state->DestroyFonts();
        state->window = nullptr;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

bool EnsureWindowClass(HINSTANCE instance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hInstance = instance;
    wc.lpfnWndProc = WindowProc;
    wc.lpszClassName = kWindowClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    if (RegisterClassExW(&wc)) return true;
    return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

} // namespace

bool ShowContentSkillBrowserDialog(
    HINSTANCE instance,
    HWND owner,
    ContentSkillBrowserDomain domain) {
    if (!instance) instance = GetModuleHandleW(nullptr);
    if (!EnsureWindowClass(instance)) return false;

    DialogState state;
    state.instance = instance;
    state.owner = owner;
    state.domain = domain;

    const UINT dpi = owner && IsWindow(owner)
        ? std::max<UINT>(USER_DEFAULT_SCREEN_DPI, GetDpiForWindow(owner))
        : USER_DEFAULT_SCREEN_DPI;
    RECT outer{
        0, 0,
        MulDiv(1020, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI),
        MulDiv(700, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI)};
    AdjustWindowRectExForDpi(
        &outer, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME,
        FALSE, WS_EX_DLGMODALFRAME, dpi);

    const HWND window = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        kWindowClass,
        domain == ContentSkillBrowserDomain::Widget
            ? L"妙喵 · 小组件创作 Skills"
            : L"妙喵 · 壁纸创作 Skills",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT,
        outer.right - outer.left, outer.bottom - outer.top,
        owner, nullptr, instance, &state);
    if (!window) return false;

    if (owner && IsWindow(owner)) EnableWindow(owner, FALSE);
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);

    MSG message{};
    bool sawQuit = false;
    int quitCode = 0;
    while (state.window) {
        const BOOL read = GetMessageW(&message, nullptr, 0, 0);
        if (read == -1) {
            if (state.window) DestroyWindow(state.window);
            break;
        }
        if (read == 0) {
            sawQuit = true;
            quitCode = static_cast<int>(message.wParam);
            if (state.window) DestroyWindow(state.window);
            break;
        }
        if (!IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    if (owner && IsWindow(owner)) {
        EnableWindow(owner, TRUE);
        SetActiveWindow(owner);
    }
    if (sawQuit) PostQuitMessage(quitCode);
    return true;
}

} // namespace miaodesk::wallpaper
