#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "turingdesk/DesktopControlService.h"

namespace turingdesk::desktop {

// M3 deliberately starts with fixed visual templates. Editing, freeform sizing,
// dragging and monitor reassignment stay out of the beginner product surface
// until the visible-runtime experience is proven on real Windows.
enum class WidgetFixedPreset {
    GlassClock,
    TodayTasks,
    WeatherGlass,
};

// UI-facing adapter for widget workflows. Win32 windows depend on this
// controller instead of DesktopWidgetStore so persistence/runtime ownership
// remains in the desktop domain services.
class DesktopWidgetController {
public:
    DesktopWidgetController() = default;

    DesktopControlResult Refresh(std::vector<wallpaper::DesktopWidget>* widgets) const;
    DesktopControlResult Find(std::wstring_view id, wallpaper::DesktopWidget* widget) const;
    DesktopControlResult RuntimeHealth(WidgetRuntimeHealth* health) const;

    // Temporary M3 showcase entry used by the production Widget page. Repeated
    // creation rotates through the three fixed presets (clock / tasks / weather)
    // so a user can compare real desktop rendering before any editor is reintroduced.
    DesktopControlResult CreateClock(
        std::wstring monitorId,
        wallpaper::DesktopWidget* created = nullptr) const;

    // Fixed-template creation. The preset owns visual style and geometry; the
    // controller auto-places it on the requested display.
    DesktopControlResult CreatePreset(
        WidgetFixedPreset preset,
        std::wstring monitorId,
        wallpaper::DesktopWidget* created = nullptr) const;

    DesktopControlResult SetEnabled(std::wstring_view id, bool enabled) const;
    DesktopControlResult Remove(std::wstring_view id) const;

private:
    DesktopControlService service_;
};

} // namespace turingdesk::desktop
