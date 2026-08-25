#pragma once

#include <windows.h>

#include <string>

namespace turingdesk::wallpaper {

enum class DesktopSurfaceTelemetryRole {
    Wallpaper,
    Widget,
};

struct DesktopSurfaceZOrderHealth {
    bool reported{};
    bool valid{};
    std::wstring detail;
};

// Read-only z-order inspection shared by DesktopShell and Widget health. This
// function never reparents or reorders HWNDs; DesktopShellHost remains the sole
// mutation owner. Child order is interpreted as top-to-bottom: desktop icons
// (SHELLDLL_DefView) must stay above TuringDesk surfaces, Widgets must stay
// above TuringDesk wallpaper surfaces.
DesktopSurfaceZOrderHealth InspectDesktopSurfaceZOrder(
    HWND surface,
    DesktopSurfaceTelemetryRole role) noexcept;

} // namespace turingdesk::wallpaper
