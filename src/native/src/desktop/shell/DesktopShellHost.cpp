#include "turingdesk/DesktopShellHost.h"

#include <algorithm>
#include <array>
#include <cwchar>
#include <string>
#include <vector>

namespace turingdesk::wallpaper {
namespace {

constexpr wchar_t kProgmanClass[] = L"Progman";
constexpr wchar_t kWorkerWClass[] = L"WorkerW";
constexpr wchar_t kDefViewClass[] = L"SHELLDLL_DefView";
constexpr wchar_t kWallpaperHostClass[] = L"TuringDesk.Native.WallpaperHost";
constexpr wchar_t kWebHostClass[] = L"TuringDesk.Native.WebWallpaperHost";
constexpr wchar_t kNativeWidgetSurfaceClass[] = L"TuringDesk.Native.WidgetSurface";
constexpr LONG_PTR kRaisedDesktopFlag = WS_EX_NOREDIRECTIONBITMAP;
constexpr UINT kSpawnWorkerMessage = 0x052C;

bool HasExtendedStyle(HWND window, LONG_PTR flag) noexcept {
    return window && IsWindow(window) && (GetWindowLongPtrW(window, GWL_EXSTYLE) & flag) != 0;
}

bool StartsWith(const wchar_t* value, const wchar_t* prefix) noexcept {
    if (!value || !prefix) return false;
    const std::size_t prefixLength = std::wcslen(prefix);
    return std::wcsncmp(value, prefix, prefixLength) == 0;
}

} // namespace

const wchar_t* DesktopShellHost::ModeKey(DesktopShellMode mode) noexcept {
    switch (mode) {
    case DesktopShellMode::RaisedDesktop: return L"raised-desktop";
    case DesktopShellMode::LegacyWorkerW: return L"legacy-workerw";
    case DesktopShellMode::ProgmanFallback: return L"progman-fallback";
    case DesktopShellMode::None: break;
    }
    return L"none";
}

const wchar_t* DesktopShellHost::RoleKey(DesktopSurfaceRole role) noexcept {
    return role == DesktopSurfaceRole::Widget ? L"widget" : L"wallpaper";
}

bool DesktopShellHost::IsWindowClass(HWND window, const wchar_t* expected) noexcept {
    if (!window || !IsWindow(window) || !expected) return false;
    wchar_t className[160]{};
    return GetClassNameW(window, className, static_cast<int>(std::size(className))) > 0 &&
           _wcsicmp(className, expected) == 0;
}

bool DesktopShellHost::IsWidgetNativeSurface(HWND window) noexcept {
    if (!IsWindowClass(window, kNativeWidgetSurfaceClass)) return false;
    wchar_t title[320]{};
    GetWindowTextW(window, title, static_cast<int>(std::size(title)));
    return StartsWith(title, L"widget-") || StartsWith(title, L"widget_");
}

bool DesktopShellHost::IsWidgetWebSurface(HWND window) noexcept {
    if (!IsWindowClass(window, kWebHostClass)) return false;
    wchar_t title[320]{};
    GetWindowTextW(window, title, static_cast<int>(std::size(title)));
    return StartsWith(title, L"widget-") || StartsWith(title, L"widget_");
}

DesktopSurfaceRole DesktopShellHost::InferRole(HWND window) noexcept {
    return IsWidgetWebSurface(window) || IsWidgetNativeSurface(window) ? DesktopSurfaceRole::Widget
                                                                         : DesktopSurfaceRole::Wallpaper;
}

HWND DesktopShellHost::LastChild(HWND parent) noexcept {
    if (!parent || !IsWindow(parent)) return nullptr;
    HWND last = nullptr;
    for (HWND child = GetWindow(parent, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) last = child;
    return last;
}

bool DesktopShellHost::TrySetParent(HWND child, HWND parent) noexcept {
    if (!child || !parent || !IsWindow(child) || !IsWindow(parent)) return false;
    SetLastError(ERROR_SUCCESS);
    const HWND previous = SetParent(child, parent);
    return previous != nullptr || GetLastError() == ERROR_SUCCESS;
}

RECT DesktopShellHost::MapDesktopRectToParent(HWND parent, const RECT& desktopBounds) noexcept {
    if (!parent || !IsWindow(parent)) return desktopBounds;
    POINT points[2] = {{desktopBounds.left, desktopBounds.top}, {desktopBounds.right, desktopBounds.bottom}};
    SetLastError(ERROR_SUCCESS);
    const int mapped = MapWindowPoints(HWND_DESKTOP, parent, points, 2);
    if (mapped == 0 && GetLastError() != ERROR_SUCCESS) return desktopBounds;
    return RECT{points[0].x, points[0].y, points[1].x, points[1].y};
}

DWORD DesktopShellHost::ExplorerProcessId(HWND progman) noexcept {
    DWORD processId = 0;
    if (progman && IsWindow(progman)) GetWindowThreadProcessId(progman, &processId);
    return processId;
}

void DesktopShellHost::RequestWallpaperLayer() const noexcept {
    if (!snapshot_.progman || !IsWindow(snapshot_.progman)) return;
    DWORD_PTR ignored = 0;
    if (HasExtendedStyle(snapshot_.progman, kRaisedDesktopFlag)) {
        SendMessageTimeoutW(snapshot_.progman, kSpawnWorkerMessage, 0xD, 0x1, SMTO_NORMAL, 1000, &ignored);
    } else {
        SendMessageTimeoutW(snapshot_.progman, kSpawnWorkerMessage, 0, 0, SMTO_NORMAL, 1000, &ignored);
        SendMessageTimeoutW(snapshot_.progman, kSpawnWorkerMessage, 0xD, 0x1, SMTO_NORMAL, 1000, &ignored);
    }
}

bool DesktopShellHost::Refresh(std::wstring* error) {
    DesktopShellSnapshot next;
    next.progman = FindWindowW(kProgmanClass, nullptr);
    if (!next.progman) {
        if (error) *error = L"DesktopShellHost: Progman not found";
        snapshot_ = {};
        return false;
    }

    snapshot_.progman = next.progman;
    RequestWallpaperLayer();

    const bool raised = HasExtendedStyle(next.progman, kRaisedDesktopFlag);
    if (raised) {
        next.shellDefView = FindWindowExW(next.progman, nullptr, kDefViewClass, nullptr);
        next.workerW = FindWindowExW(next.progman, nullptr, kWorkerWClass, nullptr);
    }

    struct LegacySearch { HWND defView{}; HWND parent{}; HWND worker{}; } legacy;
    EnumWindows([](HWND top, LPARAM raw) -> BOOL {
        auto* result = reinterpret_cast<LegacySearch*>(raw);
        const HWND defView = FindWindowExW(top, nullptr, kDefViewClass, nullptr);
        if (!defView) return TRUE;
        result->defView = defView;
        result->parent = top;
        result->worker = FindWindowExW(nullptr, top, kWorkerWClass, nullptr);
        return result->worker ? FALSE : TRUE;
    }, reinterpret_cast<LPARAM>(&legacy));

    if (!next.shellDefView) next.shellDefView = legacy.defView;
    next.legacyDefViewParent = legacy.parent;
    if (!next.workerW) next.workerW = legacy.worker;
    next.explorerPid = ExplorerProcessId(next.progman);

    if (raised && next.shellDefView) next.mode = DesktopShellMode::RaisedDesktop;
    else if (next.workerW) next.mode = DesktopShellMode::LegacyWorkerW;
    else next.mode = DesktopShellMode::ProgmanFallback;

    const bool generationChanged = snapshot_.progman != next.progman || snapshot_.workerW != next.workerW ||
                                   snapshot_.shellDefView != next.shellDefView || snapshot_.explorerPid != next.explorerPid ||
                                   snapshot_.mode != next.mode;
    if (generationChanged) ++generation_;
    next.generation = generation_;
    snapshot_ = next;

    RepairRaisedDesktopWorkerOrder();
    RepairKnownTuringDeskSurfaces();
    if (error) error->clear();
    return snapshot_.Valid();
}

bool DesktopShellHost::EnsureCurrent(std::wstring* error) {
    // Keep one definition of a valid Explorer desktop generation. The stronger
    // validator also checks mode-specific parent relationships, including the
    // Progman fallback and legacy DefView parent cases used during recovery.
    if (!CurrentGenerationValid()) return Refresh(error);
    if (error) error->clear();
    return true;
}

HWND DesktopShellHost::SurfaceParent() const noexcept {
    switch (snapshot_.mode) {
    case DesktopShellMode::RaisedDesktop:
    case DesktopShellMode::ProgmanFallback: return snapshot_.progman;
    case DesktopShellMode::LegacyWorkerW: return snapshot_.workerW;
    case DesktopShellMode::None: break;
    }
    return nullptr;
}

bool DesktopShellHost::PrepareSurface(HWND surface, bool clickThrough, std::wstring* error) const {
    if (!surface || !IsWindow(surface)) {
        if (error) *error = L"DesktopShellHost: invalid surface HWND";
        return false;
    }
    LONG_PTR style = GetWindowLongPtrW(surface, GWL_STYLE);
    style &= ~static_cast<LONG_PTR>(WS_POPUP);
    style |= WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
    SetLastError(ERROR_SUCCESS);
    if (!SetWindowLongPtrW(surface, GWL_STYLE, style) && GetLastError() != ERROR_SUCCESS) {
        if (error) *error = L"DesktopShellHost: failed to set child style, Win32=" + std::to_wstring(GetLastError());
        return false;
    }
    LONG_PTR exStyle = GetWindowLongPtrW(surface, GWL_EXSTYLE);
    exStyle |= WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
    if (clickThrough) exStyle |= WS_EX_TRANSPARENT;
    else exStyle &= ~static_cast<LONG_PTR>(WS_EX_TRANSPARENT);
    SetLastError(ERROR_SUCCESS);
    if (!SetWindowLongPtrW(surface, GWL_EXSTYLE, exStyle) && GetLastError() != ERROR_SUCCESS) {
        if (error) *error = L"DesktopShellHost: failed to set extended style, Win32=" + std::to_wstring(GetLastError());
        return false;
    }
    if (!SetLayeredWindowAttributes(surface, 0, 255, LWA_ALPHA)) {
        if (error) *error = L"DesktopShellHost: SetLayeredWindowAttributes failed, Win32=" + std::to_wstring(GetLastError());
        return false;
    }
    if (error) error->clear();
    return true;
}

void DesktopShellHost::RepairRaisedDesktopWorkerOrder() const noexcept {
    if (snapshot_.mode != DesktopShellMode::RaisedDesktop || !snapshot_.progman || !snapshot_.workerW ||
        !IsWindow(snapshot_.progman) || !IsWindow(snapshot_.workerW)) return;
    if (LastChild(snapshot_.progman) == snapshot_.workerW) return;
    SetWindowPos(snapshot_.workerW, HWND_BOTTOM, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}

void DesktopShellHost::RepairRoleOrder(HWND parent) const noexcept {
    if (!parent || !IsWindow(parent)) return;
    std::vector<HWND> wallpapers;
    std::vector<HWND> widgets;
    for (HWND child = GetWindow(parent, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        if (!IsWindow(child)) continue;
        const DesktopSurfaceRole role = InferRole(child);
        const bool known = IsWindowClass(child, kWallpaperHostClass) || IsWindowClass(child, kWebHostClass) ||
                           role == DesktopSurfaceRole::Widget;
        if (!known) continue;
        if (role != DesktopSurfaceRole::Widget && IsWindowVisible(child) == FALSE) continue;
        PrepareSurface(child, role != DesktopSurfaceRole::Widget, nullptr);
        (role == DesktopSurfaceRole::Widget ? widgets : wallpapers).push_back(child);
    }

    if (snapshot_.mode == DesktopShellMode::RaisedDesktop || snapshot_.mode == DesktopShellMode::ProgmanFallback) {
        // Top -> bottom: widgets (interactive) -> desktop icons -> wallpapers -> WorkerW.
        HWND insertAfter = HWND_BOTTOM;
        for (HWND window : wallpapers) {
            SetWindowPos(window, insertAfter, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
            insertAfter = window;
        }
        insertAfter = HWND_TOP;
        for (HWND window : widgets) {
            SetWindowPos(window, insertAfter, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
            insertAfter = window;
        }
    } else {
        HWND insertAfter = HWND_BOTTOM;
        for (HWND window : wallpapers) {
            SetWindowPos(window, insertAfter, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
            insertAfter = window;
        }
        for (HWND window : widgets) {
            SetWindowPos(window, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        }
    }
}

void DesktopShellHost::RepairKnownTuringDeskSurfaces() const {
    const HWND parent = SurfaceParent();
    if (!parent || !IsWindow(parent)) return;
    RepairRaisedDesktopWorkerOrder();
    RepairRoleOrder(parent);
}

bool DesktopShellHost::AttachSurface(HWND surface, DesktopSurfaceRole role, const RECT& desktopBounds,
                                     bool visible, std::wstring* error) {
    if (!EnsureCurrent(error)) return false;
    const HWND parent = SurfaceParent();
    if (!parent || !IsWindow(parent)) {
        if (error) *error = L"DesktopShellHost: no valid surface parent";
        return false;
    }
    if (!PrepareSurface(surface, role != DesktopSurfaceRole::Widget, error)) return false;
    if (!TrySetParent(surface, parent)) {
        if (error) *error = L"DesktopShellHost: SetParent failed, Win32=" + std::to_wstring(GetLastError());
        return false;
    }
    const RECT mapped = MapDesktopRectToParent(parent, desktopBounds);
    const LONG width = mapped.right - mapped.left;
    const LONG height = mapped.bottom - mapped.top;
    if (width <= 0 || height <= 0) {
        if (error) *error = L"DesktopShellHost: mapped surface geometry is invalid";
        return false;
    }
    const UINT flags = SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_NOOWNERZORDER |
                       (visible ? SWP_SHOWWINDOW : SWP_HIDEWINDOW);
    if (!SetWindowPos(surface, nullptr, mapped.left, mapped.top, width, height, flags | SWP_NOZORDER)) {
        if (error) *error = L"DesktopShellHost: SetWindowPos failed, Win32=" + std::to_wstring(GetLastError());
        return false;
    }
    RepairKnownTuringDeskSurfaces();
    if (snapshot_.mode != DesktopShellMode::RaisedDesktop && snapshot_.mode != DesktopShellMode::ProgmanFallback) {
        SetWindowPos(surface, role == DesktopSurfaceRole::Widget ? HWND_TOP : HWND_BOTTOM, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        RepairKnownTuringDeskSurfaces();
    }
    const auto health = InspectSurface(surface, role);
    if (!health.window || !health.parent || !health.childStyle || !health.layered || !health.geometry) {
        if (error) *error = health.detail.empty() ? L"DesktopShellHost: surface verification failed" : health.detail;
        return false;
    }
    if (error) error->clear();
    return true;
}

DesktopSurfaceHealth DesktopShellHost::InspectSurface(HWND surface, DesktopSurfaceRole role) const {
    DesktopSurfaceHealth health;
    health.mode = snapshot_.mode;
    health.role = role;
    health.window = surface && IsWindow(surface);
    if (!health.window) {
        health.detail = L"surface HWND missing";
        return health;
    }
    health.actualParent = GetParent(surface);
    health.parent = health.actualParent && health.actualParent == SurfaceParent();
    const LONG_PTR style = GetWindowLongPtrW(surface, GWL_STYLE);
    const LONG_PTR exStyle = GetWindowLongPtrW(surface, GWL_EXSTYLE);
    health.childStyle = (style & WS_CHILD) != 0;
    health.layered = (exStyle & WS_EX_LAYERED) != 0;
    health.visible = IsWindowVisible(surface) != FALSE;
    RECT rect{};
    health.geometry = GetClientRect(surface, &rect) != FALSE && rect.right > rect.left && rect.bottom > rect.top;
    if (!health.parent) health.detail = L"surface parent is not current DesktopShellHost parent";
    else if (!health.childStyle) health.detail = L"surface is missing WS_CHILD";
    else if (!health.layered) health.detail = L"surface is missing WS_EX_LAYERED";
    else if (!health.geometry) health.detail = L"surface has no drawable client geometry";
    else if (!health.visible) health.detail = L"surface HWND exists but is not visible";
    return health;
}

bool DesktopShellHost::SelfTest() noexcept {
    return _wcsicmp(ModeKey(DesktopShellMode::RaisedDesktop), L"raised-desktop") == 0 &&
           _wcsicmp(ModeKey(DesktopShellMode::LegacyWorkerW), L"legacy-workerw") == 0 &&
           _wcsicmp(RoleKey(DesktopSurfaceRole::Wallpaper), L"wallpaper") == 0 &&
           _wcsicmp(RoleKey(DesktopSurfaceRole::Widget), L"widget") == 0;
}

} // namespace turingdesk::wallpaper
