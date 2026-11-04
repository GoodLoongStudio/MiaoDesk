#pragma once

#include <windows.h>

namespace whaledesk::wallpaper {

// Canonical isolated WebView2 desktop-surface entry point for Web wallpapers.
// Wallpaper surfaces remain click-through children of the desktop shell band.
// The surface properties below are the shared lifecycle telemetry contract:
// Native widget surfaces (role 2) report the same Ready properties plus
// WhaleDesK.Native.WidgetPaintReady. Wallpaper surfaces report role 1.
// Returns -1 when the command line is not a Web desktop-surface invocation.
int TryRunWebDesktopSurfaceChild(HINSTANCE instance);

inline constexpr wchar_t kWebSurfaceEnvironmentReadyProperty[] = L"WhaleDesK.WebSurface.EnvironmentReady";
inline constexpr wchar_t kWebSurfaceControllerReadyProperty[] = L"WhaleDesK.WebSurface.ControllerReady";
inline constexpr wchar_t kWebSurfaceNavigationReadyProperty[] = L"WhaleDesK.WebSurface.NavigationReady";
inline constexpr wchar_t kWebSurfaceRoleProperty[] = L"WhaleDesK.WebSurface.Role";

} // namespace whaledesk::wallpaper