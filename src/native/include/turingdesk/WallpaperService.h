#pragma once

#include <filesystem>
#include <string>

#include "turingdesk/WallpaperLibrary.h"

namespace turingdesk::desktop {

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

// Wallpaper domain service. Owns wallpaper persistence/package transitions,
// but not Windows Shell attachment, UI, Pi protocol or Widget persistence.
class WallpaperService {
public:
    WallpaperServiceResult GetState(WallpaperState* state) const;
    WallpaperServiceResult ApplyWebPackage(const std::filesystem::path& package) const;
    WallpaperServiceResult ApplyLibraryItem(const wallpaper::WallpaperLibraryItem& item) const;
};

} // namespace turingdesk::desktop
