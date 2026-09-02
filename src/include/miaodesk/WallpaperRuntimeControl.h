#pragma once

#include <windows.h>

#include "miaodesk/AppPaths.h"

namespace miaodesk::wallpaper {

inline constexpr wchar_t kWallpaperControlWindowClass[] = L"MiaoDesk.Native.WallpaperControl";
inline constexpr UINT kWallpaperSetEnabledMessage = WM_APP + 83;
inline constexpr UINT kWallpaperReloadMessage = WM_APP + 84;

inline bool PersistedWallpaperEnabled() {
    const auto path = paths::StateFile(L"wallpaper.ini");
    if (path.empty()) return true;
    return GetPrivateProfileIntW(L"Wallpaper", L"Enabled", 1, path.c_str()) != 0;
}

inline bool NotifyWallpaperRuntimeEnabled(const bool enabled) {
    const HWND control = FindWindowW(kWallpaperControlWindowClass, nullptr);
    if (!control) return false;
    SendMessageW(control, kWallpaperSetEnabledMessage, enabled ? TRUE : FALSE, 0);
    return true;
}

inline bool NotifyWallpaperRuntimeReload() {
    const HWND control = FindWindowW(kWallpaperControlWindowClass, nullptr);
    if (!control) return false;

    // Reload must never reinterpret a persisted disabled desktop as an enable
    // request. The legacy engine historically forced Enabled=true while handling
    // reload, so route disabled state through the explicit idempotent stop verb.
    if (!PersistedWallpaperEnabled()) {
        SendMessageW(control, kWallpaperSetEnabledMessage, FALSE, 0);
        return true;
    }

    SendMessageW(control, kWallpaperReloadMessage, 0, 0);
    return true;
}

} // namespace miaodesk::wallpaper
