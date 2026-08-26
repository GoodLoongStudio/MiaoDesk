#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "turingdesk/DesktopControlService.h"

namespace turingdesk::desktop {

enum class WidgetSizePreset {
    Small,
    Medium,
    Large,
};

// UI-facing adapter for widget workflows. Win32 windows should depend on this
// controller instead of DesktopWidgetStore so persistence/runtime ownership
// remains in the desktop domain services.
class DesktopWidgetController {
public:
    DesktopWidgetController() = default;

    DesktopControlResult Refresh(std::vector<wallpaper::DesktopWidget>* widgets) const;
    DesktopControlResult Find(std::wstring_view id, wallpaper::DesktopWidget* widget) const;
    DesktopControlResult RuntimeHealth(WidgetRuntimeHealth* health) const;

    // Beginner-facing creation: callers choose only a display. The controller
    // uses the Medium preset and finds a safe desktop position automatically.
    DesktopControlResult CreateClock(std::wstring monitorId, wallpaper::DesktopWidget* created = nullptr) const;
    DesktopControlResult CreateClock(
        std::wstring monitorId,
        WidgetSizePreset size,
        wallpaper::DesktopWidget* created = nullptr) const;

    // Product-facing placement controls. UI exposes presets/display rather than
    // asking normal users to type normalized x/y/width/height values.
    DesktopControlResult SetSize(std::wstring_view id, WidgetSizePreset size) const;
    DesktopControlResult MoveToMonitor(std::wstring_view id, std::wstring monitorId) const;
    DesktopControlResult SetEnabled(std::wstring_view id, bool enabled) const;
    DesktopControlResult Remove(std::wstring_view id) const;

private:
    DesktopControlService service_;
};

} // namespace turingdesk::desktop