#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "miaodesk/WallpaperLibrary.h"
#include "miaodesk/WallpaperMonitorAssignments.h"
#include "miaodesk/WallpaperMonitorLayout.h"

namespace miaodesk::wallpaper {

enum class ResolvedWallpaperKind {
    Scene,
    Image,
    Video,
    Web,
};

struct GlobalWallpaperDescriptor {
    ResolvedWallpaperKind kind{ResolvedWallpaperKind::Scene};
    std::wstring sceneKey{L"aurora"};
    std::filesystem::path source;
};

struct ResolvedMonitorWallpaper {
    std::wstring monitorId;
    std::wstring monitorName;
    RECT region{};
    std::wstring wallpaperId;
    ResolvedWallpaperKind kind{ResolvedWallpaperKind::Scene};
    std::wstring sceneKey{L"aurora"};
    std::filesystem::path source;
    bool fallback{};
    std::wstring fallbackReason;
};

std::vector<ResolvedMonitorWallpaper> ResolveIndependentWallpapers(
    const MonitorTopology& topology,
    const WallpaperMonitorAssignments& assignments,
    const WallpaperLibrary& library,
    const GlobalWallpaperDescriptor& globalFallback);

bool IndependentLayoutHasVideo(const std::vector<ResolvedMonitorWallpaper>& resolved) noexcept;
bool IndependentLayoutHasWeb(const std::vector<ResolvedMonitorWallpaper>& resolved) noexcept;
bool SelfTestIndependentWallpaperResolution();

} // namespace miaodesk::wallpaper
