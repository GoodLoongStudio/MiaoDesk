#pragma once

#include <string>
#include <string_view>

namespace turingdesk::desktop {

enum class WidgetRuntimeAcceptanceCode : int {
    Passed = 0,
    InteractiveDesktopUnavailable = 60,
    NoEnabledWebWidget = 61,
    RuntimeHealthUnavailable = 62,
    SurfaceUnhealthy = 63,
    ReportWriteFailed = 64,
    BaselineMissing = 65,
    BaselineMismatch = 66,
};

// Real-Windows M3 probe. This intentionally consumes WidgetService's public
// runtime-health contract instead of re-enumerating Widget HWNDs here.
// The baseline phase records the enabled Widget identity set; later phases must
// prove the same configured Widgets remain present while runtime surfaces recover.
WidgetRuntimeAcceptanceCode RunWidgetRuntimeAcceptanceProbe(
    std::wstring_view phase,
    std::wstring* reportPath = nullptr,
    std::wstring* failure = nullptr);

} // namespace turingdesk::desktop
