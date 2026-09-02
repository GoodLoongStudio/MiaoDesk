#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "miaodesk/DesktopControlService.h"
#include "miaodesk/NativeWidgetPreset.h"

namespace miaodesk::desktop {

// Official built-in widgets use the same native preset identity from UI entry
// through persistence and the Direct2D host. Keeping one type means adding a new
// C++ widget does not require a second mirrored enum in the controller layer.
using WidgetFixedPreset = wallpaper::NativeWidgetPreset;

// UI-facing adapter for Widget workflows. Win32 windows depend on this
// controller instead of DesktopWidgetStore so persistence/runtime ownership
// remains in the desktop domain services.
class DesktopWidgetController {
public:
    DesktopWidgetController() = default;

    DesktopControlResult Refresh(std::vector<wallpaper::DesktopWidget>* widgets) const;
    DesktopControlResult Find(std::wstring_view id, wallpaper::DesktopWidget* widget) const;
    DesktopControlResult RuntimeHealth(WidgetRuntimeHealth* health) const;

    // Compatibility entry used by the current production Widget page. Repeated
    // creation rotates through the three built-in native presets until the UI
    // calls CreatePreset directly for each explicit card/action.
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
    DesktopControlResult MoveTo(std::wstring_view id, float x, float y) const;
    DesktopControlResult Remove(std::wstring_view id) const;

private:
    DesktopControlService service_;
};

} // namespace miaodesk::desktop
