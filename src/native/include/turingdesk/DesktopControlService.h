#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "turingdesk/WallpaperService.h"
#include "turingdesk/WidgetService.h"

namespace turingdesk::desktop {

struct DesktopControlResult {
    bool success{};
    std::wstring message;
};

struct DesktopState {
    bool enabled{true};
    std::wstring scene{L"aurora"};
    std::wstring layout{L"span"};
    std::wstring scale{L"cover"};
    int fpsCap{30};
    std::wstring imageOrWebSource;
    std::wstring videoSource;
    std::size_t widgetCount{};
};

// Atomic caller-facing desktop snapshot. UI, Pi and the future editor should
// read wallpaper + widget state through this contract instead of composing
// independent store reads that can disagree about the current desktop.
struct DesktopSnapshot {
    DesktopState desktop;
    std::vector<wallpaper::DesktopWidget> widgets;
    WidgetRuntimeHealth widgetRuntime;
};

// Facade shared by UI, Pi native tools and future editor clients. Domain
// ownership remains in WallpaperService / WidgetService; this class coordinates
// cross-domain intent and runtime activation only.
class DesktopControlService {
public:
    DesktopControlService() = default;

    DesktopControlResult GetState(DesktopState* state) const;
    DesktopControlResult GetSnapshot(DesktopSnapshot* snapshot) const;
    DesktopControlResult ApplyWebPackage(const std::filesystem::path& package) const;
    DesktopControlResult ApplyLibraryItem(const wallpaper::WallpaperLibraryItem& item) const;
    DesktopControlResult AssignLibraryItemToMonitor(
        const wallpaper::WallpaperLibraryItem& item,
        std::wstring_view monitorId,
        std::wstring_view friendlyName = {}) const;
    DesktopControlResult ClearMonitorAssignment(std::wstring_view monitorId) const;

    DesktopControlResult CreateWebWidget(
        const WebWidgetCreateRequest& request,
        wallpaper::DesktopWidget* created = nullptr) const;
    DesktopControlResult CreateNativeWidget(
        const NativeWidgetCreateRequest& request,
        wallpaper::DesktopWidget* created = nullptr) const;
    DesktopControlResult UpdateWidget(const WidgetUpdateRequest& request) const;
    DesktopControlResult RemoveWidget(std::wstring_view id) const;
    DesktopControlResult ListWidgets(std::vector<wallpaper::DesktopWidget>* widgets) const;
    DesktopControlResult FindWidget(std::wstring_view id, wallpaper::DesktopWidget* widget) const;

    DesktopControlResult EnsureRuntime() const;
};

} // namespace turingdesk::desktop