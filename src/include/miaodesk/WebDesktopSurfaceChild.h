#pragma once

#include <windows.h>

namespace miaodesk::wallpaper {

// Canonical isolated WebView2 desktop-surface entry point for Web wallpapers.
// Wallpaper surfaces remain click-through children of the desktop shell band.
// The surface properties below are the shared lifecycle telemetry contract:
// Native widget surfaces (role 2) report the same Ready properties plus
// MiaoDesk.Native.WidgetPaintReady. Wallpaper surfaces report role 1.
// Returns -1 when the command line is not a Web desktop-surface invocation.
int TryRunWebDesktopSurfaceChild(HINSTANCE instance);

inline constexpr wchar_t kWebSurfaceEnvironmentReadyProperty[] = L"MiaoDesk.WebSurface.EnvironmentReady";
inline constexpr wchar_t kWebSurfaceControllerReadyProperty[] = L"MiaoDesk.WebSurface.ControllerReady";
inline constexpr wchar_t kWebSurfaceNavigationReadyProperty[] = L"MiaoDesk.WebSurface.NavigationReady";
inline constexpr wchar_t kWebSurfaceRoleProperty[] = L"MiaoDesk.WebSurface.Role";

} // namespace miaodesk::wallpaper