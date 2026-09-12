#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "miaodesk/MiaoContentPackageManager.h"
#include "miaodesk/WallpaperService.h"
#include "miaodesk/WidgetService.h"

namespace miaodesk::desktop {

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

struct DesktopSnapshot {
    DesktopState desktop;
    std::vector<wallpaper::DesktopWidget> widgets;
    WidgetRuntimeHealth widgetRuntime;
};

class DesktopControlService {
public:
    DesktopControlService() = default;

    DesktopControlResult GetState(DesktopState* state) const;
    DesktopControlResult GetSnapshot(DesktopSnapshot* snapshot) const;

    DesktopControlResult InspectContentPackage(
        const std::filesystem::path& package,
        content::ManagedContentPackageInfo* info) const;
    DesktopControlResult ListContentPackages(
        std::vector<content::ManagedContentPackageInfo>* packages) const;
    DesktopControlResult InstallContentPackage(
        const std::filesystem::path& package,
        content::ContentPackageInstallResult* installed,
        const content::ContentPackageInstallOptions& options = {}) const;
    DesktopControlResult ResolveContentPackage(
        content::ContentKind kind,
        std::wstring_view source,
        content::ManagedContentPackageInfo* info) const;
    DesktopControlResult UninstallContentPackage(
        content::ContentKind kind,
        std::wstring_view source,
        content::ContentPackageUninstallResult* uninstalled) const;

    DesktopControlResult ApplyWebPackage(const std::filesystem::path& package) const;
    DesktopControlResult ApplyLibraryItem(const wallpaper::WallpaperLibraryItem& item) const;
    DesktopControlResult AssignLibraryItemToMonitor(
        const wallpaper::WallpaperLibraryItem& item,
        std::wstring_view monitorId,
        std::wstring_view friendlyName = {}) const;
    DesktopControlResult ClearMonitorAssignment(std::wstring_view monitorId) const;

    DesktopControlResult CreateNativeWidget(
        const NativeWidgetCreateRequest& request,
        wallpaper::DesktopWidget* created = nullptr) const;

    DesktopControlResult CreateContentWidget(
        const ContentWidgetCreateRequest& request,
        wallpaper::DesktopWidget* created = nullptr) const {
        WidgetService service;
        const auto result = service.CreateContent(request, created);
        if (!result.success) return {false, result.message};
        const auto runtime = ReloadWidgetRuntime();
        if (!runtime.success) return runtime;
        return {true, result.message};
    }

    DesktopControlResult GetContentWidgetSettings(
        std::wstring_view id,
        ContentWidgetSettingsSnapshot* settings) const {
        WidgetService service;
        const auto result = service.GetContentSettings(id, settings);
        return {result.success, result.message};
    }

    DesktopControlResult GetContentWidgetParameters(
        std::wstring_view id,
        content::ContentParameterValues* values) const {
        WidgetService service;
        const auto result = service.GetContentParameters(id, values);
        return {result.success, result.message};
    }

    DesktopControlResult SetContentWidgetParameters(
        std::wstring_view id,
        const content::ContentParameterValues& changes) const {
        WidgetService service;
        const auto result = service.SetContentParameters(id, changes);
        return {result.success, result.message};
    }

    DesktopControlResult SetContentWidgetParameter(
        std::wstring_view id,
        std::wstring_view key,
        content::ContentParameterValue value) const {
        WidgetService service;
        const auto result = service.SetContentParameter(id, key, std::move(value));
        return {result.success, result.message};
    }

    DesktopControlResult ResetContentWidgetParameters(std::wstring_view id) const {
        WidgetService service;
        const auto result = service.ResetContentParameters(id);
        return {result.success, result.message};
    }

    DesktopControlResult UpdateWidget(const WidgetUpdateRequest& request) const;
    DesktopControlResult RemoveWidget(std::wstring_view id) const;
    DesktopControlResult ListWidgets(std::vector<wallpaper::DesktopWidget>* widgets) const;
    DesktopControlResult FindWidget(std::wstring_view id, wallpaper::DesktopWidget* widget) const;

    DesktopControlResult SetWallpaperEnabled(bool enabled) const;
    DesktopControlResult EnsureRuntime() const;
    DesktopControlResult ReloadWidgetRuntime() const;
};

} // namespace miaodesk::desktop
