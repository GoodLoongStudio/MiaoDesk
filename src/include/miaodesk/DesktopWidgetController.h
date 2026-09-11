#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "miaodesk/DesktopControlService.h"
#include "miaodesk/NativeWidgetPreset.h"

namespace miaodesk::desktop {

// The production UI keeps the three familiar preset names while the controller
// prefers their Scene Content packages whenever those packages are available.
// Native presets remain the development/runtime fallback for missing packages.
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

    DesktopControlResult SetContentParameters(
        std::wstring_view id,
        const content::ContentParameterValues& changes) const {
        return service_.SetContentWidgetParameters(id, changes);
    }

    DesktopControlResult SetContentParameter(
        std::wstring_view id,
        std::wstring_view key,
        content::ContentParameterValue value) const;

    DesktopControlResult ResetContentParameters(std::wstring_view id) const;

    DesktopControlResult GetTodayTasks(TodayTaskSnapshot* snapshot) const {
        return service_.GetTodayTasks(snapshot);
    }

    DesktopControlResult ReplaceTodayTasks(const std::vector<TodayTaskItem>& items) const {
        return service_.ReplaceTodayTasks(items);
    }

    DesktopControlResult SetTodayTaskCompleted(std::wstring_view id, bool completed) const {
        return service_.SetTodayTaskCompleted(id, completed);
    }

    DesktopControlResult SetEnabled(std::wstring_view id, bool enabled) const;
    DesktopControlResult MoveTo(std::wstring_view id, float x, float y) const;
    DesktopControlResult Remove(std::wstring_view id) const;

private:
    DesktopControlService service_;
};

} // namespace miaodesk::desktop
