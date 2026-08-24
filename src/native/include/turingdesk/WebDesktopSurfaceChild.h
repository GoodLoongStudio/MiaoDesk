#pragma once

#include <windows.h>

namespace turingdesk::wallpaper {

// New shell-compatible WebView2 child path. Returns -1 when the command line is
// not a Web desktop-surface invocation so the legacy handler can remain as a
// temporary fallback during migration.
int TryRunWebDesktopSurfaceChild(HINSTANCE instance);

inline constexpr wchar_t kWebSurfaceEnvironmentReadyProperty[] = L"TuringDesk.WebSurface.EnvironmentReady";
inline constexpr wchar_t kWebSurfaceControllerReadyProperty[] = L"TuringDesk.WebSurface.ControllerReady";
inline constexpr wchar_t kWebSurfaceNavigationReadyProperty[] = L"TuringDesk.WebSurface.NavigationReady";
inline constexpr wchar_t kWebSurfaceRoleProperty[] = L"TuringDesk.WebSurface.Role";

} // namespace turingdesk::wallpaper
