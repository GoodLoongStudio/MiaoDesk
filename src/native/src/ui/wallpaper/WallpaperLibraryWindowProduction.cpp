// Production Desktop Library entry.
//
// The legacy WallpaperLibraryWindow implementation has been retired from the
// shipping path. Production now compiles the V2 product shell directly so
// users and real-Windows acceptance always exercise the same UI implementation.
// Persistence and Widget runtime state continue to flow through domain
// controllers/services; this translation unit owns no Store/INI/Shell state.

#include "WallpaperLibraryWindowV2.cpp"

#include <commctrl.h>
#include <array>
#include <algorithm>

namespace {

// These sections remain implemented behind the product shell, but they are not
// part of the current user-facing settings surface. Keep the service/domain
// code intact so a future product decision can restore them without rebuilding
// persistence or runtime ownership.
constexpr wchar_t kDesktopLibraryClassName[] = L"TuringDesk.Native.DesktopLibrary";
constexpr int kWallpaperNavId = 6110;
constexpr int kWidgetsNavId = 6111;
constexpr int kPlaylistsNavId = 6112;
constexpr int kDisplaysNavId = 6113;
constexpr int kRulesNavId = 6114;
constexpr int kPerformanceNavId = 6115;
constexpr int kAiNavId = 6116;
constexpr UINT_PTR kCompactNavSubclassId = 0x54444E41; // "TDNA"

bool IsDesktopLibraryWindow(HWND window) {
    if (!window || !IsWindow(window)) return false;
    wchar_t className[128]{};
    return GetClassNameW(window, className, static_cast<int>(std::size(className))) > 0 &&
           _wcsicmp(className, kDesktopLibraryClassName) == 0;
}

void ApplyCompactDesktopNavigation(HWND window) {
    if (!IsDesktopLibraryWindow(window)) return;

    for (const int id : {kPlaylistsNavId, kDisplaysNavId, kRulesNavId, kPerformanceNavId}) {
        if (HWND item = GetDlgItem(window, id)) ShowWindow(item, SW_HIDE);
    }

    const HWND wallpaper = GetDlgItem(window, kWallpaperNavId);
    const HWND widgets = GetDlgItem(window, kWidgetsNavId);
    const HWND ai = GetDlgItem(window, kAiNavId);
    if (!wallpaper || !widgets || !ai) return;

    RECT wallpaperRect{};
    RECT widgetRect{};
    RECT aiRect{};
    if (!GetWindowRect(wallpaper, &wallpaperRect) ||
        !GetWindowRect(widgets, &widgetRect) ||
        !GetWindowRect(ai, &aiRect)) return;

    MapWindowPoints(HWND_DESKTOP, window, reinterpret_cast<POINT*>(&wallpaperRect), 2);
    MapWindowPoints(HWND_DESKTOP, window, reinterpret_cast<POINT*>(&widgetRect), 2);
    MapWindowPoints(HWND_DESKTOP, window, reinterpret_cast<POINT*>(&aiRect), 2);

    const int gap = std::max(0, static_cast<int>(widgetRect.top - wallpaperRect.bottom));
    const int width = std::max(1, static_cast<int>(widgetRect.right - widgetRect.left));
    const int height = std::max(1, static_cast<int>(widgetRect.bottom - widgetRect.top));
    const int aiTop = static_cast<int>(widgetRect.bottom) + gap;

    MoveWindow(ai, static_cast<int>(widgetRect.left), aiTop, width, height, TRUE);
    ShowWindow(ai, SW_SHOW);
}

LRESULT CALLBACK CompactDesktopNavigationSubclass(
    HWND window, UINT message, WPARAM wParam, LPARAM lParam,
    UINT_PTR, DWORD_PTR) {
    const LRESULT result = DefSubclassProc(window, message, wParam, lParam);

    switch (message) {
    case WM_SIZE:
    case WM_DPICHANGED:
    case WM_SHOWWINDOW:
        ApplyCompactDesktopNavigation(window);
        break;
    case WM_NCDESTROY:
        RemoveWindowSubclass(window, CompactDesktopNavigationSubclass, kCompactNavSubclassId);
        break;
    default:
        break;
    }
    return result;
}

void EnsureCompactDesktopNavigation(HWND window) {
    if (!IsDesktopLibraryWindow(window)) return;
    SetWindowSubclass(window, CompactDesktopNavigationSubclass, kCompactNavSubclassId, 0);
    ApplyCompactDesktopNavigation(window);
}

void CALLBACK DesktopLibraryShowEvent(
    HWINEVENTHOOK, DWORD event, HWND window, LONG objectId, LONG childId,
    DWORD, DWORD) {
    if (event != EVENT_OBJECT_SHOW || objectId != OBJID_WINDOW || childId != CHILDID_SELF) return;
    EnsureCompactDesktopNavigation(window);
}

class CompactDesktopNavigationBootstrap final {
public:
    CompactDesktopNavigationBootstrap() {
        hook_ = SetWinEventHook(
            EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW, nullptr, DesktopLibraryShowEvent,
            GetCurrentProcessId(), 0, WINEVENT_OUTOFCONTEXT);
    }

    ~CompactDesktopNavigationBootstrap() {
        if (hook_) UnhookWinEvent(hook_);
    }

private:
    HWINEVENTHOOK hook_{};
};

CompactDesktopNavigationBootstrap gCompactDesktopNavigationBootstrap;

} // namespace
