#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "miaodesk/DesktopControlService.h"
#include "miaodesk/NativeWidgetPreset.h"

namespace miaodesk::desktop {

// The current production UI still exposes three familiar fixed presets. During
// migration, GlassClock/WeatherGlass may resolve to Content packages while
// TodayTasks keeps its Native fallback until a real task data provider exists.
using WidgetFixedPreset = wallpaper::NativeWidgetPreset;

class DesktopWidgetController {
public:
    DesktopWidgetController() = default;

    DesktopControlResult Refresh(std::vector<wallpaper::DesktopWidget>* widgets) const;
    DesktopControlResult Find(std::wstring_view id, wallpaper::DesktopWidget* widget) const;
    DesktopControlResult RuntimeHealth(WidgetRuntimeHealth* health) const;

    DesktopControlResult CreateClock(
        std::wstring monitorId,
        wallpaper::DesktopWidget* created = nullptr) const;

    DesktopControlResult CreatePreset(
        WidgetFixedPreset preset,
        std::wstring monitorId,
        wallpaper::DesktopWidget* created = nullptr) const;

    DesktopControlResult GetContentSettings(
        std::wstring_view id,
        ContentWidgetSettingsSnapshot* settings) const {
        return service_.GetContentWidgetSettings(id, settings);
    }

    DesktopControlResult GetContentParameters(
        std::wstring_view id,
        content::ContentParameterValues* values) const;

    DesktopControlResult SetContentParameter(
        std::wstring_view id,
        std::wstring_view key,
        content::ContentParameterValue value) const;

    DesktopControlResult ResetContentParameters(std::wstring_view id) const;

    DesktopControlResult SetEnabled(std::wstring_view id, bool enabled) const;
    DesktopControlResult MoveTo(std::wstring_view id, float x, float y) const;
    DesktopControlResult Remove(std::wstring_view id) const;

private:
    DesktopControlService service_;
};

} // namespace miaodesk::desktop
