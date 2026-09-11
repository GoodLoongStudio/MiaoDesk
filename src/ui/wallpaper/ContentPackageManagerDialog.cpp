#include "miaodesk/ContentPackageManagerDialog.h"

#include "miaodesk/DesktopControlService.h"

#include <shellapi.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace miaodesk::wallpaper {
namespace {

constexpr wchar_t kWindowClass[] = L"MiaoDesk.Native.ContentPackageManager";
constexpr int kListId = 7001;
constexpr int kRefreshId = 7002;
constexpr int kOpenFolderId = 7003;
constexpr int kUninstallId = 7004;
constexpr int kCloseId = 7005;

HMENU ControlId(int id) {
    return reinterpret_cast<HMENU>(static_cast<INT_PTR>(id));
}

const wchar_t* KindText(content::ContentKind kind) noexcept {
    return kind == content::ContentKind::Widget ? L"小组件" : L"壁纸";
}

const wchar_t* OriginText(content::ManagedContentPackageOrigin origin) noexcept {
    switch (origin) {
    case content::ManagedContentPackageOrigin::BuiltIn: return L"内置";
    case content::ManagedContentPackageOrigin::UserManaged: return L"用户安装";
    case content::ManagedContentPackageOrigin::External: return L"外部";
    }
    return L"未知";
}

const wchar_t* RuntimeText(content::ContentRuntimeKind runtime) noexcept {
    switch (runtime) {
    case content::ContentRuntimeKind::Scene: return L"Scene";
    case content::ContentRuntimeKind::Web: return L"Web";
    }
    return L"Unknown";
}

struct DialogState {
    HINSTANCE instance{};
    HWND owner{};
    content::ContentKind kind{content::ContentKind::Wallpaper};
    HWND window{};
    HWND heading{};
    HWND list{};
    HWND details{};
    HWND refreshButton{};
    HWND openButton{};
    HWND uninstallButton{};
    HWND closeButton{};
    HFONT font{};
    desktop::DesktopControlService control;
    std::vector<content::ManagedContentPackageInfo> packages;
    bool changed{};

    int S(int value) const noexcept {
        const UINT dpi = window ? std::max<UINT>(USER_DEFAULT_SCREEN_DPI, GetDpiForWindow(window))
                                : USER_DEFAULT_SCREEN_DPI;
        return MulDiv(value, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
    }

    void ApplyFont(HWND child) const {
        if (child && font) SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }

    bool CreateControls() {
        font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        heading = CreateWindowExW(0, L"STATIC",
            kind == content::ContentKind::Widget ? L"已安装小组件内容包" : L"已安装壁纸内容包",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            0, 0, 10, 10, window, nullptr, instance, nullptr);
        list = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
            0, 0, 10, 10, window, ControlId(kListId), instance, nullptr);
        details = CreateWindowExW(WS_EX_CLIENTEDGE, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            0, 0, 10, 10, window, nullptr, instance, nullptr);
        refreshButton = CreateWindowExW(0, L"BUTTON", L"刷新",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            0, 0, 10, 10, window, ControlId(kRefreshId), instance, nullptr);
        openButton = CreateWindowExW(0, L"BUTTON", L"打开所在目录",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            0, 0, 10, 10, window, ControlId(kOpenFolderId), instance, nullptr);
        uninstallButton = CreateWindowExW(0, L"BUTTON", L"卸载",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            0, 0, 10, 10, window, ControlId(kUninstallId), instance, nullptr);
        closeButton = CreateWindowExW(0, L"BUTTON", L"关闭",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            0, 0, 10, 10, window, ControlId(kCloseId), instance, nullptr);

        for (HWND child : {heading, list, details, refreshButton, openButton, uninstallButton, closeButton})
            ApplyFont(child);
        return heading && list && details && refreshButton && openButton && uninstallButton && closeButton;
    }

    void Layout() const {
        if (!window) return;
        RECT rc{};
        GetClientRect(window, &rc);
        const int margin = S(16);
        const int gap = S(10);
        const int headingH = S(28);
        const int buttonH = S(34);
        const int detailsH = S(150);
        const int clientW = std::max(1, static_cast<int>(rc.right - rc.left));
        const int clientH = std::max(1, static_cast<int>(rc.bottom - rc.top));

        SetWindowPos(heading, nullptr, margin, margin, clientW - margin * 2, headingH,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        const int listTop = margin + headingH + gap;
        const int buttonsTop = clientH - margin - buttonH;
        const int detailsTop = std::max(listTop + S(80), buttonsTop - gap - detailsH);
        SetWindowPos(list, nullptr, margin, listTop, clientW - margin * 2,
                     std::max(S(80), detailsTop - gap - listTop), SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(details, nullptr, margin, detailsTop, clientW - margin * 2, detailsH,
                     SWP_NOZORDER | SWP_NOACTIVATE);

        const int refreshW = S(78);
        const int openW = S(118);
        const int uninstallW = S(86);
        const int closeW = S(86);
        SetWindowPos(refreshButton, nullptr, margin, buttonsTop, refreshW, buttonH,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(openButton, nullptr, margin + refreshW + gap, buttonsTop, openW, buttonH,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(uninstallButton, nullptr, margin + refreshW + gap + openW + gap,
                     buttonsTop, uninstallW, buttonH, SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(closeButton, nullptr, clientW - margin - closeW, buttonsTop, closeW, buttonH,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }

    int SelectedIndex() const {
        if (!list) return -1;
        const LRESULT index = SendMessageW(list, LB_GETCURSEL, 0, 0);
        return index == LB_ERR ? -1 : static_cast<int>(index);
    }

    const content::ManagedContentPackageInfo* SelectedPackage() const {
        const int index = SelectedIndex();
        if (index < 0 || static_cast<std::size_t>(index) >= packages.size()) return nullptr;
        return &packages[static_cast<std::size_t>(index)];
    }

    void UpdateDetails() const {
        const auto* package = SelectedPackage();
        if (!package) {
            SetWindowTextW(details, L"没有已安装的内容包。\r\n\r\n可从壁纸页或组件页的添加菜单安装 .mdwall / .mdwidget。 ");
            EnableWindow(openButton, FALSE);
            EnableWindow(uninstallButton, FALSE);
            return;
        }

        std::wstring text;
        text += L"名称：" + (package->name.empty() ? std::wstring(L"(未命名)") : package->name) + L"\r\n";
        text += L"作者：" + (package->author.empty() ? std::wstring(L"(未知)") : package->author) + L"\r\n";
        text += L"版本：" + (package->version.empty() ? std::wstring(L"(未声明)") : package->version) + L"\r\n";
        text += L"Content ID：" + package->source + L"\r\n";
        text += L"类型 / Runtime：" + std::wstring(KindText(package->kind)) + L" / " + RuntimeText(package->runtime) + L"\r\n";
        text += L"来源：" + std::wstring(OriginText(package->origin)) + L"\r\n";
        text += L"位置：" + package->packageRoot.wstring();
        SetWindowTextW(details, text.c_str());
        EnableWindow(openButton, !package->packageRoot.empty());
        EnableWindow(uninstallButton,
                     package->origin == content::ManagedContentPackageOrigin::UserManaged);
    }

    bool Refresh(bool showError) {
        std::vector<content::ManagedContentPackageInfo> all;
        const auto result = control.ListContentPackages(&all);
        if (!result.success) {
            if (showError) {
                MessageBoxW(window,
                            result.message.empty() ? L"无法读取已安装内容包。" : result.message.c_str(),
                            L"MiaoDesk 内容包", MB_OK | MB_ICONERROR);
            }
            return false;
        }

        std::wstring previousSource;
        if (const auto* selected = SelectedPackage()) previousSource = selected->source;

        packages.clear();
        for (auto& package : all)
            if (package.kind == kind) packages.push_back(std::move(package));

        SendMessageW(list, LB_RESETCONTENT, 0, 0);
        int selectedIndex = -1;
        for (std::size_t i = 0; i < packages.size(); ++i) {
            const auto& package = packages[i];
            std::wstring row = L"[" + std::wstring(OriginText(package.origin)) + L"] " +
                (package.name.empty() ? package.source : package.name);
            if (!package.version.empty()) row += L"  ·  v" + package.version;
            row += L"  ·  " + package.source;
            SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(row.c_str()));
            if (!previousSource.empty() && package.source == previousSource)
                selectedIndex = static_cast<int>(i);
        }
        if (selectedIndex < 0 && !packages.empty()) selectedIndex = 0;
        if (selectedIndex >= 0) SendMessageW(list, LB_SETCURSEL, selectedIndex, 0);
        UpdateDetails();
        return true;
    }

    void OpenSelectedFolder() const {
        const auto* package = SelectedPackage();
        if (!package || package->packageRoot.empty()) return;
        const HINSTANCE opened = ShellExecuteW(window, L"open", package->packageRoot.c_str(),
                                               nullptr, nullptr, SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(opened) <= 32) {
            MessageBoxW(window, L"无法打开内容包目录。", L"MiaoDesk 内容包",
                        MB_OK | MB_ICONERROR);
        }
    }

    void UninstallSelected() {
        const auto* package = SelectedPackage();
        if (!package) return;
        if (package->origin != content::ManagedContentPackageOrigin::UserManaged) {
            MessageBoxW(window, L"内置内容包不能卸载。", L"MiaoDesk 内容包",
                        MB_OK | MB_ICONINFORMATION);
            return;
        }

        const std::wstring name = package->name.empty() ? package->source : package->name;
        const std::wstring prompt = L"卸载“" + name + L"”？\r\n\r\n" + package->source +
            L"\r\n\r\n如果仍有桌面小组件实例或显示器壁纸分配引用它，MiaoDesk 会拒绝卸载并提示先处理引用。";
        if (MessageBoxW(window, prompt.c_str(), L"MiaoDesk 内容包",
                        MB_YESNO | MB_ICONQUESTION) != IDYES) return;

        const content::ContentKind selectedKind = package->kind;
        const std::wstring source = package->source;
        content::ContentPackageUninstallResult removed;
        const auto result = control.UninstallContentPackage(selectedKind, source, &removed);
        if (!result.success) {
            MessageBoxW(window,
                        result.message.empty() ? L"内容包卸载失败。" : result.message.c_str(),
                        L"MiaoDesk 内容包", MB_OK | MB_ICONWARNING);
            return;
        }
        changed = true;
        MessageBoxW(window, result.message.c_str(), L"MiaoDesk 内容包",
                    MB_OK | MB_ICONINFORMATION);
        Refresh(false);
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
        state->Refresh(true);
        return 0;
    case WM_SIZE:
        state->Layout();
        return 0;
    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        const int notification = HIWORD(wParam);
        if (id == kListId && notification == LBN_SELCHANGE) {
            state->UpdateDetails();
            return 0;
        }
        if (id == kRefreshId && notification == BN_CLICKED) {
            state->Refresh(true);
            return 0;
        }
        if (id == kOpenFolderId && notification == BN_CLICKED) {
            state->OpenSelectedFolder();
            return 0;
        }
        if (id == kUninstallId && notification == BN_CLICKED) {
            state->UninstallSelected();
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

bool ShowContentPackageManagerDialog(
    HINSTANCE instance,
    HWND owner,
    content::ContentKind initialKind) {
    if (!instance) instance = GetModuleHandleW(nullptr);
    if (!EnsureWindowClass(instance)) return false;

    DialogState state;
    state.instance = instance;
    state.owner = owner;
    state.kind = initialKind;

    const UINT dpi = owner && IsWindow(owner)
        ? std::max<UINT>(USER_DEFAULT_SCREEN_DPI, GetDpiForWindow(owner))
        : USER_DEFAULT_SCREEN_DPI;
    RECT outer{0, 0, MulDiv(760, dpi, USER_DEFAULT_SCREEN_DPI),
                    MulDiv(520, dpi, USER_DEFAULT_SCREEN_DPI)};
    AdjustWindowRectExForDpi(&outer, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME,
                             FALSE, WS_EX_DLGMODALFRAME, dpi);

    const HWND window = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        kWindowClass,
        initialKind == content::ContentKind::Widget ? L"管理小组件内容包" : L"管理壁纸内容包",
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
    return state.changed;
}

} // namespace miaodesk::wallpaper
