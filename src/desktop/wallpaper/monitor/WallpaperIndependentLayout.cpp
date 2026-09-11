#include "miaodesk/WallpaperIndependentLayout.h"
#include "miaodesk/BuiltinWallpaperCatalog.h"
#include "miaodesk/MiaoContentPackage.h"
#include "miaodesk/MiaoContentPackageManager.h"
#include "miaodesk/WebWallpaperHost.h"

#include <windows.h>

#include <algorithm>
#include <fstream>
#include <optional>
#include <system_error>

namespace fs = std::filesystem;

namespace miaodesk::wallpaper {
namespace {

ResolvedMonitorWallpaper MakeFallback(const MonitorInfo& monitor, const RECT& region,
                                      const GlobalWallpaperDescriptor& fallback,
                                      std::wstring reason) {
    ResolvedMonitorWallpaper result;
    result.monitorId = StableMonitorKey(monitor);
    result.monitorName = monitor.friendlyName.empty() ? monitor.deviceName : monitor.friendlyName;
    result.region = region;
    result.kind = fallback.kind;
    result.sceneKey = fallback.sceneKey;
    result.source = fallback.source;
    result.fallback = true;
    result.fallbackReason = std::move(reason);
    return result;
}

std::wstring SceneKeyForLibraryId(std::wstring_view id) {
    const auto* definition = FindBuiltinWallpaper(id);
    return definition ? std::wstring(definition->runtimeKey) : std::wstring{};
}

bool IsContentId(std::wstring_view id) noexcept {
    return id.size() > content::MiaoContentPackageManager::kSourcePrefix.size() &&
           id.substr(0, content::MiaoContentPackageManager::kSourcePrefix.size()) ==
               content::MiaoContentPackageManager::kSourcePrefix;
}

struct ResolvedContentWallpaper {
    ResolvedWallpaperKind kind{ResolvedWallpaperKind::Scene};
    fs::path source;
};

std::optional<ResolvedContentWallpaper> ResolveContentWallpaper(std::wstring_view id) {
    if (!IsContentId(id)) return std::nullopt;

    content::ManagedContentPackageInfo package;
    std::wstring error;
    if (!content::MiaoContentPackageManager::Resolve(
            content::ContentKind::Wallpaper, id, &package, &error)) return std::nullopt;

    if (package.runtime == content::ContentRuntimeKind::Scene)
        return ResolvedContentWallpaper{ResolvedWallpaperKind::Scene, package.packageRoot};
    if (package.runtime != content::ContentRuntimeKind::Web) return std::nullopt;

    content::LoadedMiaoContentPackage loaded;
    if (!content::MiaoContentPackage::Load(package.packageRoot, &loaded, &error) ||
        loaded.manifest.kind != content::ContentKind::Wallpaper ||
        loaded.manifest.runtime != content::ContentRuntimeKind::Web) {
        return std::nullopt;
    }

    fs::path entry;
    if (!content::MiaoContentPackage::ResolvePackagePath(
            loaded.root, loaded.manifest.entry, &entry, &error) ||
        !WebWallpaperProcessSet::IsSupportedSource(entry.wstring())) {
        return std::nullopt;
    }
    return ResolvedContentWallpaper{ResolvedWallpaperKind::Web, std::move(entry)};
}

bool SourceAvailable(const WallpaperLibraryItem& item) {
    if (item.kind == LibraryWallpaperKind::Scene) {
        if (IsContentId(item.id)) {
            const auto resolved = ResolveContentWallpaper(item.id);
            return resolved && resolved->kind == ResolvedWallpaperKind::Scene;
        }

        // Built-in scenes are key-driven and have no source path. Legacy
        // canonical Scene records may still carry a physical .mdwall path.
        if (item.source.empty()) return FindBuiltinWallpaper(item.id) != nullptr;
        std::error_code ec;
        if (fs::is_directory(item.source, ec) && _wcsicmp(item.source.extension().c_str(), L".mdwall") == 0)
            return true;
        ec.clear();
        if (fs::is_regular_file(item.source, ec)) {
            fs::path current = item.source.parent_path();
            while (!current.empty()) {
                if (_wcsicmp(current.extension().c_str(), L".mdwall") == 0) return true;
                const fs::path parent = current.parent_path();
                if (parent == current) break;
                current = parent;
            }
        }
        return false;
    }
    if (item.kind == LibraryWallpaperKind::Web) {
        if (IsContentId(item.id)) {
            const auto resolved = ResolveContentWallpaper(item.id);
            return resolved && resolved->kind == ResolvedWallpaperKind::Web;
        }
        return WebWallpaperProcessSet::IsSupportedSource(item.source.wstring());
    }
    std::error_code ec;
    return !item.source.empty() && fs::exists(item.source, ec) && fs::is_regular_file(item.source, ec);
}

} // namespace

std::vector<ResolvedMonitorWallpaper> ResolveIndependentWallpapers(
    const MonitorTopology& topology,
    const WallpaperMonitorAssignments& assignments,
    const WallpaperLibrary& library,
    const GlobalWallpaperDescriptor& globalFallback) {
    std::vector<ResolvedMonitorWallpaper> result;
    if (!topology.Valid()) return result;

    const auto regions = DrawRegionsInHost(topology, LayoutMode::Independent);
    result.reserve(assignments.Items().size());
    for (std::size_t i = 0; i < topology.monitors.size(); ++i) {
        const auto& monitor = topology.monitors[i];
        const RECT region = i < regions.size() ? regions[i] : RECT{};
        const auto assignedId = assignments.WallpaperIdFor(monitor);

        // An unassigned monitor must stay untouched. The old behavior painted
        // the global fallback on every unassigned display, so choosing one
        // monitor still modified the rest of the virtual desktop. Independent
        // mode now creates surfaces only for explicitly assigned monitors.
        if (!assignedId) continue;

        const auto item = library.Find(*assignedId);
        if (!item) {
            // A freshly installed canonical package may already be assigned by
            // content:<id> while the settings window still holds a pre-install
            // WallpaperLibrary snapshot. Resolve the current package runtime
            // directly so both Scene and Web content survive a stale UI cache.
            if (const auto contentWallpaper = ResolveContentWallpaper(*assignedId)) {
                ResolvedMonitorWallpaper resolved;
                resolved.monitorId = StableMonitorKey(monitor);
                resolved.monitorName = monitor.friendlyName.empty() ? monitor.deviceName : monitor.friendlyName;
                resolved.region = region;
                resolved.wallpaperId = *assignedId;
                resolved.kind = contentWallpaper->kind;
                resolved.source = contentWallpaper->source;
                result.push_back(std::move(resolved));
                continue;
            }

            result.push_back(MakeFallback(monitor, region, globalFallback, L"保存的壁纸库项目已不存在"));
            result.back().wallpaperId = *assignedId;
            continue;
        }
        if (item->kind == LibraryWallpaperKind::Unknown) {
            result.push_back(MakeFallback(monitor, region, globalFallback, L"壁纸类型无法识别"));
            result.back().wallpaperId = item->id;
            continue;
        }
        if (!SourceAvailable(*item)) {
            result.push_back(MakeFallback(monitor, region, globalFallback, L"壁纸源文件已离线、被移动或 URL 无效"));
            result.back().wallpaperId = item->id;
            continue;
        }

        ResolvedMonitorWallpaper resolved;
        resolved.monitorId = StableMonitorKey(monitor);
        resolved.monitorName = monitor.friendlyName.empty() ? monitor.deviceName : monitor.friendlyName;
        resolved.region = region;
        resolved.wallpaperId = item->id;
        switch (item->kind) {
        case LibraryWallpaperKind::Scene:
            resolved.kind = ResolvedWallpaperKind::Scene;
            resolved.sceneKey = SceneKeyForLibraryId(item->id);
            if (const auto current = ResolveContentWallpaper(item->id)) {
                // Resolve stable content:<id> immediately before creating the
                // runtime plan. library.ini can contain an old physical path;
                // the manifest id remains authoritative.
                resolved.kind = current->kind;
                resolved.source = current->source;
            } else {
                resolved.source = item->source;
            }
            break;
        case LibraryWallpaperKind::Image:
            resolved.kind = ResolvedWallpaperKind::Image;
            resolved.source = item->source;
            break;
        case LibraryWallpaperKind::Video:
            resolved.kind = ResolvedWallpaperKind::Video;
            resolved.source = item->source;
            break;
        case LibraryWallpaperKind::Web:
            resolved.kind = ResolvedWallpaperKind::Web;
            if (const auto current = ResolveContentWallpaper(item->id))
                resolved.source = current->source;
            else
                resolved.source = item->source;
            break;
        case LibraryWallpaperKind::Unknown:
            break;
        }
        result.push_back(std::move(resolved));
    }
    return result;
}

bool IndependentLayoutHasVideo(const std::vector<ResolvedMonitorWallpaper>& resolved) noexcept {
    return std::any_of(resolved.begin(), resolved.end(), [](const auto& item) {
        return item.kind == ResolvedWallpaperKind::Video;
    });
}

bool IndependentLayoutHasWeb(const std::vector<ResolvedMonitorWallpaper>& resolved) noexcept {
    return std::any_of(resolved.begin(), resolved.end(), [](const auto& item) {
        return item.kind == ResolvedWallpaperKind::Web;
    });
}

bool SelfTestIndependentWallpaperResolution() {
    std::error_code ec;
    const fs::path root = fs::temp_directory_path() /
        (L"MiaoDesk-IndependentLayout-SelfTest-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    fs::create_directories(root, ec);
    if (ec) return false;

    WallpaperLibrary library(root / L"Library");
    WallpaperMonitorAssignments assignments(root / L"assignments.ini");
    std::wstring error;
    bool ok = library.Load(&error) && assignments.Load(&error);
    ok = ok && library.UpsertScene(L"scene-aurora", L"Aurora", &error);
    ok = ok && library.UpsertScene(L"scene-neon", L"Neon", &error);

    MonitorTopology topology;
    topology.virtualBounds = {0, 0, 3840, 1080};
    topology.primaryBounds = {0, 0, 1920, 1080};
    topology.monitors = {
        {nullptr, {0, 0, 1920, 1080}, true, 96, 96, L"A", L"monitor-a", L"Panel A"},
        {nullptr, {1920, 0, 3840, 1080}, false, 96, 96, L"B", L"monitor-b", L"Panel B"},
    };

    // Single-monitor contract: assigning B must leave A completely absent from
    // the resolved surface list instead of drawing a fallback there.
    ok = ok && assignments.Assign(topology.monitors[1], L"scene-neon", &error);

    GlobalWallpaperDescriptor fallback;
    fallback.kind = ResolvedWallpaperKind::Scene;
    fallback.sceneKey = L"aurora";
    const auto resolved = ResolveIndependentWallpapers(topology, assignments, library, fallback);
    ok = ok && resolved.size() == 1;
    if (resolved.size() == 1) {
        ok = ok && resolved[0].monitorId == L"monitor-b";
        ok = ok && !resolved[0].fallback;
        ok = ok && resolved[0].kind == ResolvedWallpaperKind::Scene;
        ok = ok && resolved[0].sceneKey == L"neon";
        ok = ok && resolved[0].region.left == 1920;
        ok = ok && resolved[0].region.right == 3840;
    }
    ok = ok && !IndependentLayoutHasVideo(resolved) && !IndependentLayoutHasWeb(resolved);

    fs::remove_all(root, ec);
    return ok;
}

} // namespace miaodesk::wallpaper
