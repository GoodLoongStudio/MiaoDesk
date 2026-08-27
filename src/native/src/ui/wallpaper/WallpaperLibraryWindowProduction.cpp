// Production Desktop Library entry.
//
// The shipping Desktop Library is the V2 implementation itself. Keep this
// wrapper intentionally thin: it may hide product sections that are not
// currently exposed, but it must never run a second geometry/layout system on
// top of WallpaperLibraryWindowV2. The V2 WndProc owns WM_SIZE/WM_DPICHANGED
// and the standard WS_OVERLAPPEDWINDOW non-client frame owns resize hit tests.

#include "WallpaperLibraryWindowV2.cpp"

#include <commctrl.h>
#include <algorithm>
#include <array>

namespace {

constexpr wchar_t kDesktopLibraryClassName[] = L"TuringDesk.Native.DesktopLibrary";
constexpr int kWallpaperNavId = 6110;
constexpr int kWidgetsNavId = 6111;
constexpr int kPlaylistsNavId = 6112;
constexpr int kDisplaysNavId = 6113;
constexpr int kRulesNavId = 6114;
constexpr int kPerformanceNavId = 6115;
constexpr int kAiNavId = 6116;
constexpr UINT_PTR kCompactNavSubclassId = 0x54444E56; // "TDNV"

bool IsDesktopLibraryWindow(HWND window) {
    if (!window || !IsWindow(window)) return false;
    wchar_t className[128]{};
    return GetClassNameW(window, className, static_cast<int>(std::size(className))) > 0 &&
           _wcsicmp(className, kDesktopLibraryClassName) == 0;
}

RECT ChildRect(HWND parent, HWND child) {
    RECT rect{};
    if (!parent || !child || !GetWindowRect(child, &rect)) return rect;
    MapWindowPoints(HWND_DESKTOP, parent, reinterpret_cast<POINT*>(&rect), 2);
    return rect;
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

    const RECT wallpaperRect = ChildRect(window, wallpaper);
    const RECT widgetRect = ChildRect(window, widgets);
    const int gap = std::max(0, static_cast<int>(widgetRect.top - wallpaperRect.bottom));
    const int width = std::max(1, static_cast<int>(widgetRect.right - widgetRect.left));
    const int height = std::max(1, static_cast<int>(widgetRect.bottom - widgetRect.top));
    const int aiTop = static_cast<int>(widgetRect.bottom) + gap;

    MoveWindow(ai, static_cast<int>(widgetRect.left), aiTop, width, height, TRUE);
    ShowWindow(ai, SW_SHOW);
}

LRESULT CALLBACK CompactNavSubclass(
    HWND window, UINT message, WPARAM wParam, LPARAM lParam,
    UINT_PTR, DWORD_PTR) {
    const LRESULT result = DefSubclassProc(window, message, wParam, lParam);

    // V2 has already completed its one canonical layout when control returns
    // here. Only re-compact the navigation strip; never move content controls.
    switch (message) {
    case WM_SIZE:
    case WM_DPICHANGED:
    case WM_SHOWWINDOW:
        ApplyCompactDesktopNavigation(window);
        break;
    case WM_NCDESTROY:
        RemoveWindowSubclass(window, CompactNavSubclass, kCompactNavSubclassId);
        break;
    default:
        break;
    }
    return result;
}

void EnsureCompactDesktopNavigation(HWND window) {
    if (!IsDesktopLibraryWindow(window)) return;
    SetWindowSubclass(window, CompactNavSubclass, kCompactNavSubclassId, 0);
    ApplyCompactDesktopNavigation(window);
}

void CALLBACK DesktopLibraryShowEvent(
    HWINEVENTHOOK, DWORD event, HWND window, LONG objectId, LONG childId,
    DWORD, DWORD) {
    if (event != EVENT_OBJECT_SHOW || objectId != OBJID_WINDOW || childId != CHILDID_SELF) return;
    EnsureCompactDesktopNavigation(window);
}

class CompactNavigationBootstrap final {
public:
    CompactNavigationBootstrap() {
        hook_ = SetWinEventHook(
            EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW, nullptr, DesktopLibraryShowEvent,
            GetCurrentProcessId(), 0, WINEVENT_OUTOFCONTEXT);
    }

    ~CompactNavigationBootstrap() {
        if (hook_) UnhookWinEvent(hook_);
    }

private:
    HWINEVENTHOOK hook_{};
};

CompactNavigationBootstrap gCompactNavigationBootstrap;

} // namespace
