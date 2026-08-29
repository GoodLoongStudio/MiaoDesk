#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "miaodesk/WallpaperLibrary.h"

namespace miaodesk::desktop {

struct WallpaperState {
    bool enabled{true};
    std::wstring scene{L"aurora"};
    std::wstring layout{L"span"};
    std::wstring scale{L"cover"};
    int fpsCap{30};
    std::wstring imageOrWebSource;
    std::wstring videoSource;
};

struct WallpaperServiceResult {
    bool success{};
    std::wstring message;
};

// Wallpaper domain service. Owns wallpaper persistence/package/assignment
// transitions, but not Windows Shell attachment, UI, Pi protocol or Widget
// persistence.
class WallpaperService {
public:
    WallpaperServiceResult GetState(WallpaperState* state) const;
    WallpaperServiceResult SetEnabled(bool enabled) const;
    WallpaperServiceResult ApplyWebPackage(const std::filesystem::path& package) const;
    WallpaperServiceResult ApplyLibraryItem(const wallpaper::WallpaperLibraryItem& item) const;
    WallpaperServiceResult AssignLibraryItemToMonitor(
        const wallpaper::WallpaperLibraryItem& item,
        std::wstring_view monitorId,
        std::wstring_view friendlyName = {}) const;
    WallpaperServiceResult ClearMonitorAssignment(std::wstring_view monitorId) const;
};

} // namespace miaodesk::desktop
