#pragma once

#include <string>

#include "miaodesk/PerformanceService.h"

namespace miaodesk::wallpaper {

// UI-facing adapter for performance policy. Windows controls should use this
// instead of reading or writing wallpaper.ini directly.
class PerformanceUiAdapter {
public:
    bool Load(PerformanceConfig* config, std::wstring* error = nullptr) const;
    bool Save(const PerformanceConfig& config, std::wstring* error = nullptr) const;

private:
    static bool AssignError(const desktop::PerformanceServiceResult& result, std::wstring* error);
    desktop::PerformanceService service_;
};

} // namespace miaodesk::wallpaper
