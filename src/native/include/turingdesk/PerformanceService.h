#pragma once

#include <string>

#include "turingdesk/WallpaperPerformancePolicy.h"

namespace turingdesk::desktop {

struct PerformanceServiceResult {
    bool success{};
    std::wstring message;
};

// Domain boundary for wallpaper performance persistence. UI code should express
// desired policy through this service rather than reading/writing wallpaper.ini.
class PerformanceService {
public:
    PerformanceService() = default;

    PerformanceServiceResult GetConfig(wallpaper::PerformanceConfig* config) const;
    PerformanceServiceResult SaveConfig(const wallpaper::PerformanceConfig& config) const;
};

} // namespace turingdesk::desktop
