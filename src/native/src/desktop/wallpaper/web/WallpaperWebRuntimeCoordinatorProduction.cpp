// Production bridge for WallpaperWebRuntimeCoordinator.
//
// The legacy coordinator still contains one SetWindowPos call for the native
// wallpaper host in independent-layout refresh. Production intercepts that
// geometry mutation and delegates the full parent/style/geometry/z-order
// transaction to DesktopShellHost::EnsureSurface. Non-wallpaper child-window
// SetWindowPos calls, if introduced later, continue to use Win32 directly.
//
// Delete this bridge once EnsureIndependentHostBounds is removed from the
// legacy coordinator source.

#include <windows.h>

#include "turingdesk/DesktopShellHost.h"

#include <algorithm>
#include <cwchar>
#include <iterator>
#include <string>

namespace {

bool IsWallpaperHost(HWND window) noexcept {
    if (!window || !IsWindow(window)) return false;
    wchar_t className[128]{};
    if (GetClassNameW(window, className, static_cast<int>(std::size(className))) <= 0) return false;
    return _wcsicmp(className, L"TuringDesk.Native.WallpaperHost") == 0;
}

RECT ParentClientRectToDesktop(HWND window, int x, int y, int width, int height) noexcept {
    const LONG safeWidth = std::max(1, width);
    const LONG safeHeight = std::max(1, height);
    RECT result{x, y, x + safeWidth, y + safeHeight};
    const HWND parent = window ? GetParent(window) : nullptr;
    if (!parent || parent == HWND_DESKTOP) return result;

    POINT corners[2] = {{result.left, result.top}, {result.right, result.bottom}};
    SetLastError(ERROR_SUCCESS);
    if (MapWindowPoints(parent, HWND_DESKTOP, corners, 2) == 0 && GetLastError() != ERROR_SUCCESS) return result;
    return RECT{corners[0].x, corners[0].y, corners[1].x, corners[1].y};
}

bool RequestedVisibility(HWND window, UINT flags) noexcept {
    if ((flags & SWP_SHOWWINDOW) != 0) return true;
    if ((flags & SWP_HIDEWINDOW) != 0) return false;
    return window && IsWindowVisible(window) != FALSE;
}

BOOL WINAPI TuringDeskCoordinatorSetWindowPos(
    HWND window,
    HWND insertAfter,
    int x,
    int y,
    int width,
    int height,
    UINT flags) {
    if (IsWallpaperHost(window) && !(flags & SWP_NOMOVE) && !(flags & SWP_NOSIZE)) {
        const RECT desktopBounds = ParentClientRectToDesktop(window, x, y, width, height);
        turingdesk::wallpaper::DesktopShellHost shell;
        std::wstring error;
        const bool visible = RequestedVisibility(window, flags);
        const bool ok = shell.EnsureSurface(
            window,
            turingdesk::wallpaper::DesktopSurfaceRole::Wallpaper,
            desktopBounds,
            visible,
            &error);
        if (!ok) SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return ok ? TRUE : FALSE;
    }
    return ::SetWindowPos(window, insertAfter, x, y, width, height, flags);
}

} // namespace

#define SetWindowPos TuringDeskCoordinatorSetWindowPos
#include "WallpaperWebRuntimeCoordinator.cpp"
#undef SetWindowPos
