// Production Desktop Library entry.
//
// The legacy WallpaperLibraryWindow implementation has been retired from the
// shipping path. Production now compiles the V2 product shell directly so
// users and real-Windows acceptance always exercise the same UI implementation.
// Persistence and Widget runtime state continue to flow through domain
// controllers/services; this translation unit owns no Store/INI/Shell state.

#include "WallpaperLibraryWindowV2.cpp"

#include <commctrl.h>
#include <algorithm>
#include <array>

namespace {

// These sections remain implemented behind the product shell, but they are not
// part of the current user-facing settings surface. Keep the service/domain
// code intact so a future product decision can restore them without rebuilding
// persistence or runtime ownership.
constexpr wchar_t kDesktopLibraryClassName[] = L"TuringDesk.Native.DesktopLibrary";
constexpr int kSearchControlId = 6101;
constexpr int kAddControlId = 6102;
constexpr int kWallpaperNavId = 6110;
constexpr int kWidgetsNavId = 6111;
constexpr int kPlaylistsNavId = 6112;
constexpr int kDisplaysNavId = 6113;
constexpr int kRulesNavId = 6114;
constexpr int kPerformanceNavId = 6115;
constexpr int kAiNavId = 6116;
constexpr int kWallpaperGridId = 6120;
constexpr int kTargetComboId = 6130;
constexpr int kApplyButtonId = 6131;
constexpr int kFavoriteButtonId = 6132;
constexpr int kRemoveButtonId = 6133;
constexpr int kWidgetCreateButtonId = 6140;
constexpr int kWidgetToggleButtonId = 6141;
constexpr int kWidgetRemoveButtonId = 6142;
constexpr int kWidgetRefreshButtonId = 6143;
constexpr UINT_PTR kDesktopLayoutSubclassId = 0x54444E41; // "TDNA"
constexpr UINT_PTR kLayoutSentinelSubclassId = 0x54444C53; // "TDLS"
constexpr UINT kResponsiveLayoutMessage = WM_APP + 321;

bool IsDesktopLibraryWindow(HWND window) {
    if (!window || !IsWindow(window)) return false;
    wchar_t className[128]{};
    return GetClassNameW(window, className, static_cast<int>(std::size(className))) > 0 &&
           _wcsicmp(className, kDesktopLibraryClassName) == 0;
}

int ScaleForWindow(HWND window, int logicalPx) {
    const UINT dpi = window ? GetDpiForWindow(window) : USER_DEFAULT_SCREEN_DPI;
    return MulDiv(logicalPx, static_cast<int>(dpi ? dpi : USER_DEFAULT_SCREEN_DPI), USER_DEFAULT_SCREEN_DPI);
}

RECT ChildRect(HWND parent, HWND child) {
    RECT rect{};
    if (!parent || !child || !GetWindowRect(child, &rect)) return rect;
    MapWindowPoints(HWND_DESKTOP, parent, reinterpret_cast<POINT*>(&rect), 2);
    return rect;
}

void MoveIfNeeded(HWND parent, HWND child, int x, int y, int width, int height) {
    if (!parent || !child || !IsWindow(child)) return;
    width = std::max(1, width);
    height = std::max(1, height);
    const RECT current = ChildRect(parent, child);
    if (current.left == x && current.top == y &&
        current.right - current.left == width && current.bottom - current.top == height) return;
    MoveWindow(child, x, y, width, height, TRUE);
}

HWND FindBottomStatus(HWND window) {
    struct Candidate {
        HWND window{};
        LONG top{LONG_MIN};
    } candidate;

    EnumChildWindows(window, [](HWND child, LPARAM data) -> BOOL {
        wchar_t className[32]{};
        if (GetClassNameW(child, className, static_cast<int>(std::size(className))) <= 0 ||
            _wcsicmp(className, L"Static") != 0) return TRUE;

        auto* candidate = reinterpret_cast<Candidate*>(data);
        RECT rect = ChildRect(GetParent(child), child);
        if (rect.top > candidate->top) {
            candidate->top = rect.top;
            candidate->window = child;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&candidate));

    return candidate.window;
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

    MoveIfNeeded(window, ai, static_cast<int>(widgetRect.left), aiTop, width, height);
    ShowWindow(ai, SW_SHOW);
}

void ApplyResponsiveDesktopLayout(HWND window) {
    if (!IsDesktopLibraryWindow(window)) return;
    ApplyCompactDesktopNavigation(window);

    RECT client{};
    GetClientRect(window, &client);
    const int width = std::max(1, static_cast<int>(client.right - client.left));
    const int height = std::max(1, static_cast<int>(client.bottom - client.top));
    const int sidebarW = ScaleForWindow(window, 208);
    const int margin = ScaleForWindow(window, 18);
    const int topH = ScaleForWindow(window, 58);
    const int footerH = ScaleForWindow(window, 58);
    const int gap = ScaleForWindow(window, 8);
    const int contentLeft = sidebarW;
    const int right = width - margin;

    const HWND wallpaperGrid = GetDlgItem(window, kWallpaperGridId);
    const bool wallpaperPage = wallpaperGrid && IsWindowVisible(wallpaperGrid);
    const HWND search = GetDlgItem(window, kSearchControlId);
    const HWND add = GetDlgItem(window, kAddControlId);

    if (wallpaperPage) {
        const int addW = ScaleForWindow(window, 96);
        const int addX = right - addW;
        MoveIfNeeded(window, add, addX, ScaleForWindow(window, 11), addW, ScaleForWindow(window, 36));
        ShowWindow(add, SW_SHOW);

        // Reserve the left title block first, then fit search into the true gap
        // before the Add button. This avoids title/search/add overlap at 125%-200% DPI.
        const int searchX = contentLeft + margin + ScaleForWindow(window, 220) + ScaleForWindow(window, 12);
        const int searchRight = addX - ScaleForWindow(window, 12);
        const int searchW = searchRight - searchX;
        if (search && searchW >= ScaleForWindow(window, 180)) {
            MoveIfNeeded(window, search, searchX, ScaleForWindow(window, 12), searchW, ScaleForWindow(window, 34));
            ShowWindow(search, SW_SHOW);
        } else if (search) {
            ShowWindow(search, SW_HIDE);
        }
    } else {
        if (search) ShowWindow(search, SW_HIDE);
        if (add) ShowWindow(add, SW_HIDE);
    }

    const int footerTop = height - footerH;
    const int buttonY = footerTop + ScaleForWindow(window, 11);
    const int buttonH = ScaleForWindow(window, 36);
    const int statusLeft = contentLeft + margin;
    int statusRight = right;

    if (wallpaperPage) {
        const int actionW = ScaleForWindow(window, 108);
        const int smallW = ScaleForWindow(window, 88);
        const int targetW = ScaleForWindow(window, 182);

        const int applyX = right - actionW;
        const int removeX = applyX - gap - smallW;
        const int favoriteX = removeX - gap - smallW;
        const int targetX = favoriteX - gap - targetW;

        MoveIfNeeded(window, GetDlgItem(window, kApplyButtonId), applyX, buttonY, actionW, buttonH);
        MoveIfNeeded(window, GetDlgItem(window, kRemoveButtonId), removeX, buttonY, smallW, buttonH);
        MoveIfNeeded(window, GetDlgItem(window, kFavoriteButtonId), favoriteX, buttonY, smallW, buttonH);
        MoveIfNeeded(window, GetDlgItem(window, kTargetComboId), targetX, buttonY, targetW, ScaleForWindow(window, 180));
        statusRight = targetX - ScaleForWindow(window, 12);
    } else {
        const int normalW = ScaleForWindow(window, 84);
        const int createW = ScaleForWindow(window, 154);
        const int removeX = right - normalW;
        const int toggleX = removeX - gap - normalW;
        const int refreshX = toggleX - gap - normalW;
        const int createX = refreshX - gap - createW;

        MoveIfNeeded(window, GetDlgItem(window, kWidgetRemoveButtonId), removeX, buttonY, normalW, buttonH);
        MoveIfNeeded(window, GetDlgItem(window, kWidgetToggleButtonId), toggleX, buttonY, normalW, buttonH);
        MoveIfNeeded(window, GetDlgItem(window, kWidgetRefreshButtonId), refreshX, buttonY, normalW, buttonH);
        MoveIfNeeded(window, GetDlgItem(window, kWidgetCreateButtonId), createX, buttonY, createW, buttonH);
        statusRight = createX - ScaleForWindow(window, 12);
    }

    // The original V2 layout gave status a fixed `contentWidth - 600` width,
    // which overlapped the combo/buttons at high DPI. Fit it only into the
    // remaining space and let Windows ellipsize long diagnostics.
    if (HWND status = FindBottomStatus(window)) {
        LONG_PTR style = GetWindowLongPtrW(status, GWL_STYLE);
        if ((style & SS_ENDELLIPSIS) == 0) {
            SetWindowLongPtrW(status, GWL_STYLE, style | SS_ENDELLIPSIS);
        }
        const int statusW = std::max(0, statusRight - statusLeft);
        if (statusW >= ScaleForWindow(window, 72)) {
            MoveIfNeeded(window, status, statusLeft, footerTop + ScaleForWindow(window, 18), statusW, ScaleForWindow(window, 26));
            ShowWindow(status, SW_SHOW);
        } else {
            ShowWindow(status, SW_HIDE);
        }
    }

    // Keep top and footer bands separated from the content even at unusual DPI.
    (void)topH;
}

LRESULT CALLBACK LayoutSentinelSubclass(
    HWND child, UINT message, WPARAM wParam, LPARAM lParam,
    UINT_PTR, DWORD_PTR) {
    const HWND parent = GetParent(child);
    const LRESULT result = DefSubclassProc(child, message, wParam, lParam);
    if (message == WM_WINDOWPOSCHANGED && parent && IsWindow(parent)) {
        PostMessageW(parent, kResponsiveLayoutMessage, 0, 0);
    } else if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(child, LayoutSentinelSubclass, kLayoutSentinelSubclassId);
    }
    return result;
}

void AttachLayoutSentinel(HWND window) {
    if (HWND status = FindBottomStatus(window)) {
        SetWindowSubclass(status, LayoutSentinelSubclass, kLayoutSentinelSubclassId, 0);
    }
}

LRESULT CALLBACK DesktopLayoutSubclass(
    HWND window, UINT message, WPARAM wParam, LPARAM lParam,
    UINT_PTR, DWORD_PTR) {
    if (message == kResponsiveLayoutMessage) {
        ApplyResponsiveDesktopLayout(window);
        return 0;
    }

    const LRESULT result = DefSubclassProc(window, message, wParam, lParam);
    switch (message) {
    case WM_SIZE:
    case WM_DPICHANGED:
    case WM_SHOWWINDOW:
    case WM_COMMAND:
        AttachLayoutSentinel(window);
        ApplyResponsiveDesktopLayout(window);
        break;
    case WM_NCDESTROY:
        RemoveWindowSubclass(window, DesktopLayoutSubclass, kDesktopLayoutSubclassId);
        break;
    default:
        break;
    }
    return result;
}

void EnsureResponsiveDesktopLayout(HWND window) {
    if (!IsDesktopLibraryWindow(window)) return;
    SetWindowSubclass(window, DesktopLayoutSubclass, kDesktopLayoutSubclassId, 0);
    AttachLayoutSentinel(window);
    ApplyResponsiveDesktopLayout(window);
}

void CALLBACK DesktopLibraryShowEvent(
    HWINEVENTHOOK, DWORD event, HWND window, LONG objectId, LONG childId,
    DWORD, DWORD) {
    if (event != EVENT_OBJECT_SHOW || objectId != OBJID_WINDOW || childId != CHILDID_SELF) return;
    EnsureResponsiveDesktopLayout(window);
}

class ResponsiveDesktopLayoutBootstrap final {
public:
    ResponsiveDesktopLayoutBootstrap() {
        hook_ = SetWinEventHook(
            EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW, nullptr, DesktopLibraryShowEvent,
            GetCurrentProcessId(), 0, WINEVENT_OUTOFCONTEXT);
    }

    ~ResponsiveDesktopLayoutBootstrap() {
        if (hook_) UnhookWinEvent(hook_);
    }

private:
    HWINEVENTHOOK hook_{};
};

ResponsiveDesktopLayoutBootstrap gResponsiveDesktopLayoutBootstrap;

} // namespace
