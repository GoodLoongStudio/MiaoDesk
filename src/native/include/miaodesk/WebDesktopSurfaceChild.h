#pragma once

#include <windows.h>

namespace miaodesk::wallpaper {

// Canonical isolated WebView2 desktop-surface entry point. Wallpaper surfaces
// remain click-through; Widget surfaces expose a full-surface native drag layer
// and persist their normalized monitor-local desktop position.
// Drag geometry stays in Win32 LONG coordinates for ARM64/MSVC parity.
// Returns -1 when the command line is not a Web desktop-surface invocation.
int TryRunWebDesktopSurfaceChild(HINSTANCE instance);

inline constexpr wchar_t kWebSurfaceEnvironmentReadyProperty[] = L"MiaoDesk.WebSurface.EnvironmentReady";
inline constexpr wchar_t kWebSurfaceControllerReadyProperty[] = L"MiaoDesk.WebSurface.ControllerReady";
inline constexpr wchar_t kWebSurfaceNavigationReadyProperty[] = L"MiaoDesk.WebSurface.NavigationReady";
inline constexpr wchar_t kWebSurfaceRoleProperty[] = L"MiaoDesk.WebSurface.Role";

} // namespace miaodesk::wallpaper