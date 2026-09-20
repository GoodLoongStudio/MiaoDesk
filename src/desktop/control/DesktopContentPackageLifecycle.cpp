#include "miaodesk/DesktopControlService.h"

#include "miaodesk/RuntimeLogger.h"
#include "miaodesk/WallpaperLibrary.h"
#include "miaodesk/WallpaperMonitorAssignments.h"

#include <algorithm>
#include <vector>

namespace miaodesk::desktop {
namespace {

bool SameSource(std::wstring_view left, std::wstring_view right) noexcept {
    return left == right;
}

} // namespace

DesktopControlResult DesktopControlService::ListContentPackages(
    std::vector<content::ManagedContentPackageInfo>* packages) const {
    if (!packages) return {false, L"Content package list 输出不能为空。"};
    std::wstring error;
    if (!content::MiaoContentPackageManager::List(packages, &error)) {
        miaodesk::log::Error(L"DesktopControl", L"ListContentPackages 失败: " + error);
        return {false, error.empty() ? L"无法读取已安装内容包。" : error};
    }
    return {true, L"已读取内容包：" + std::to_wstring(packages->size()) + L" 个。"};
}

DesktopControlResult DesktopControlService::UninstallContentPackage(
    content::ContentKind kind,
    std::wstring_view source,
    content::ContentPackageUninstallResult* uninstalled) const {
    if (!uninstalled) return {false, L"Content package uninstall result 输出不能为空。"};
    *uninstalled = {};

    content::ManagedContentPackageInfo package;
    const auto resolved = ResolveContentPackage(kind, source, &package);
    if (!resolved.success) return resolved;
    if (package.origin == content::ManagedContentPackageOrigin::BuiltIn)
        return {false, L"内置内容包不能卸载，只能在产品层隐藏或停用。"};

    if (kind == content::ContentKind::Widget) {
        WidgetService widgetService;
        std::vector<wallpaper::DesktopWidget> widgets;
        const auto listed = widgetService.List(&widgets);
        if (!listed.success) return {false, listed.message};

        const auto referenced = std::find_if(widgets.begin(), widgets.end(), [&](const auto& widget) {
            return SameSource(widget.source.wstring(), source);
        });
        if (referenced != widgets.end()) {
            return {false,
                    L"该小组件包仍被桌面实例引用：" + referenced->title +
                        L"。请先删除相关小组件实例，再卸载内容包。"};
        }
    } else {
        WallpaperService wallpaperService;
        WallpaperState state;
        const auto stateResult = wallpaperService.GetState(&state);
        if (!stateResult.success) return {false, stateResult.message};
        if (SameSource(state.scene, source))
            return {false, L"该壁纸包仍是当前全局壁纸，请先切换到其他壁纸。"};

        wallpaper::WallpaperMonitorAssignments assignments;
        std::wstring assignmentError;
        if (!assignments.Load(&assignmentError))
            return {false, assignmentError.empty() ? L"无法读取显示器壁纸分配。" : assignmentError};
        const auto assigned = std::find_if(assignments.Items().begin(), assignments.Items().end(), [&](const auto& item) {
            return SameSource(item.wallpaperId, source);
        });
        if (assigned != assignments.Items().end()) {
            const std::wstring monitor = assigned->lastFriendlyName.empty()
                ? assigned->monitorId
                : assigned->lastFriendlyName;
            return {false,
                    L"该壁纸包仍分配给显示器“" + monitor +
                        L"”。请先切换或清除该显示器的壁纸分配。"};
        }
    }

    std::wstring error;
    content::ContentPackageUninstallResult removed;
    if (!content::MiaoContentPackageManager::Uninstall(kind, source, &removed, &error)) {
        miaodesk::log::Error(L"DesktopControl", L"UninstallContentPackage 失败: " + error);
        return {false, error.empty() ? L"内容包卸载失败。" : error};
    }

    std::wstring cleanupWarning;
    if (kind == content::ContentKind::Wallpaper) {
        // Remove the library index after the package has atomically left the
        // catalog. A failure here cannot resurrect or partially uninstall the
        // package; it only leaves stale UI metadata that can be repaired later.
        wallpaper::WallpaperLibrary library;
        std::wstring libraryError;
        if (library.Load(&libraryError)) {
            if (library.Find(source) && !library.Remove(source, false, &libraryError))
                cleanupWarning = libraryError;
        } else {
            cleanupWarning = libraryError;
        }
    }

    *uninstalled = removed;
    std::wstring message = L"已卸载内容包：" + removed.package.name + L"（" + removed.package.source + L"）";
    if (removed.cleanupDeferred)
        message += L"。包已移出运行时目录，物理垃圾将在后续维护时清理。";
    if (!cleanupWarning.empty()) {
        miaodesk::log::Warn(L"DesktopControl", L"内容包已卸载，但壁纸库索引清理失败: " + cleanupWarning);
        message += L"。壁纸库索引清理需要后续修复。";
    }
    miaodesk::log::Info(L"DesktopControl", message);
    return {true, std::move(message)};
}

} // namespace miaodesk::desktop
