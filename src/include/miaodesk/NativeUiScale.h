#pragma once

#include <windows.h>

#include <algorithm>
#include <cstdint>

namespace miaodesk::ui {

// Windows DPI alone is not enough for MiaoDesk's desktop UI. A 2560x1440 or
// 3840x2160 monitor can still report 96 DPI when the user keeps Windows scaling
// at 100%, which makes a fixed 12-14 px UI font look much smaller than the same
// UI on a 1920x1080 display.
//
// Keep geometry DPI-driven, but let font size also react to the monitor's pixel
// resolution. The landscape/portrait-neutral baseline is 1920x1080:
//   1920x1080 -> 96  (1.00x)
//   2560x1440 -> 128 (1.33x)
//   3840x2160 -> 192 (2.00x)
// The result is capped at 2x so an unusually large virtual/remote desktop cannot
// create absurdly large controls. Native DPI can still win when Windows scaling
// is larger than the resolution-derived factor.
constexpr UINT ResolutionFontDpiForSize(int width, int height) noexcept {
    if (width <= 0 || height <= 0) return USER_DEFAULT_SCREEN_DPI;
    const std::int64_t longEdge = std::max(width, height);
    const std::int64_t shortEdge = std::min(width, height);
    const std::int64_t fromLong = longEdge * USER_DEFAULT_SCREEN_DPI / 1920;
    const std::int64_t fromShort = shortEdge * USER_DEFAULT_SCREEN_DPI / 1080;
    const std::int64_t derived = std::min(fromLong, fromShort);
    return static_cast<UINT>(std::clamp<std::int64_t>(
        derived, USER_DEFAULT_SCREEN_DPI, USER_DEFAULT_SCREEN_DPI * 2));
}

inline HMONITOR MonitorForWindow(HWND window) noexcept {
    if (window && IsWindow(window))
        return MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
    return MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
}

inline UINT ResolutionFontDpi(HWND window) noexcept {
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    const HMONITOR monitor = MonitorForWindow(window);
    if (!monitor || !GetMonitorInfoW(monitor, &info)) return USER_DEFAULT_SCREEN_DPI;
    const int width = static_cast<int>(info.rcMonitor.right - info.rcMonitor.left);
    const int height = static_cast<int>(info.rcMonitor.bottom - info.rcMonitor.top);
    return ResolutionFontDpiForSize(width, height);
}

inline UINT NativeDpi(HWND window) noexcept {
    UINT dpi = USER_DEFAULT_SCREEN_DPI;
    if (window && IsWindow(window)) dpi = GetDpiForWindow(window);
    else dpi = GetDpiForSystem();
    return dpi ? dpi : USER_DEFAULT_SCREEN_DPI;
}

inline UINT EffectiveFontDpi(HWND window) noexcept {
    return std::max(NativeDpi(window), ResolutionFontDpi(window));
}

inline int ScaleFontPx(HWND window, int logicalPx) noexcept {
    return MulDiv(logicalPx, static_cast<int>(EffectiveFontDpi(window)),
                  USER_DEFAULT_SCREEN_DPI);
}

inline HFONT CreateUiFont(HWND window, int logicalPx, int weight = FW_NORMAL,
                          const wchar_t* face = L"Segoe UI Variable Text") {
    return CreateFontW(-ScaleFontPx(window, logicalPx), 0, 0, 0, weight,
                       FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face);
}

static_assert(ResolutionFontDpiForSize(1920, 1080) == 96);
static_assert(ResolutionFontDpiForSize(2560, 1440) == 128);
static_assert(ResolutionFontDpiForSize(1440, 2560) == 128);
static_assert(ResolutionFontDpiForSize(3840, 2160) == 192);

} // namespace miaodesk::ui
