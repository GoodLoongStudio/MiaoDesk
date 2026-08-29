#pragma once

#include <windows.h>

namespace miaodesk::wallpaper {

inline constexpr wchar_t kWallpaperControlWindowClass[] = L"MiaoDesk.Native.WallpaperControl";
inline constexpr UINT kWallpaperSetEnabledMessage = WM_APP + 83;
inline constexpr UINT kWallpaperReloadMessage = WM_APP + 84;

inline bool NotifyWallpaperRuntimeEnabled(const bool enabled) {
    const HWND control = FindWindowW(kWallpaperControlWindowClass, nullptr);
    if (!control) return false;
    SendMessageW(control, kWallpaperSetEnabledMessage, enabled ? TRUE : FALSE, 0);
    return true;
}

inline bool NotifyWallpaperRuntimeReload() {
    const HWND control = FindWindowW(kWallpaperControlWindowClass, nullptr);
    if (!control) return false;
    SendMessageW(control, kWallpaperReloadMessage, 0, 0);
    return true;
}

} // namespace miaodesk::wallpaper
