#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "turingdesk/DesktopControlService.h"

namespace turingdesk::desktop {

// UI-facing adapter for widget workflows. Win32 windows should depend on this
// controller instead of DesktopWidgetStore so persistence/runtime ownership
// remains in the desktop domain services.
class DesktopWidgetController {
public:
    DesktopWidgetController() = default;

    DesktopControlResult Refresh(std::vector<wallpaper::DesktopWidget>* widgets) const;
    DesktopControlResult Find(std::wstring_view id, wallpaper::DesktopWidget* widget) const;
    DesktopControlResult CreateClock(std::wstring monitorId, wallpaper::DesktopWidget* created = nullptr) const;
    DesktopControlResult SetEnabled(std::wstring_view id, bool enabled) const;
    DesktopControlResult Remove(std::wstring_view id) const;

private:
    DesktopControlService service_;
};

} // namespace turingdesk::desktop
