#include "turingdesk/PerformanceUiAdapter.h"

namespace turingdesk::wallpaper {

bool PerformanceUiAdapter::AssignError(const desktop::PerformanceServiceResult& result, std::wstring* error) {
    if (result.success) return true;
    if (error) *error = result.message;
    return false;
}

bool PerformanceUiAdapter::Load(PerformanceConfig* config, std::wstring* error) const {
    return AssignError(service_.GetConfig(config), error);
}

bool PerformanceUiAdapter::Save(const PerformanceConfig& config, std::wstring* error) const {
    return AssignError(service_.SaveConfig(config), error);
}

} // namespace turingdesk::wallpaper
