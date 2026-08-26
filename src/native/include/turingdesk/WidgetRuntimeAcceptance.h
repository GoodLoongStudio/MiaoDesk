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
    SequenceOutOfOrder = 67,
};

// Real-Windows M3 probe. This intentionally consumes WidgetService's public
// runtime-health contract instead of re-enumerating Widget HWNDs here.
// The baseline phase records the enabled Widget identity set and current
// interactive Windows session; later phases must prove both remain continuous
// while runtime processes/HWNDs are still allowed to be recreated during recovery.
// Missing session evidence maps to BaselineMissing; a changed session maps to
// BaselineMismatch, keeping session continuity inside the existing baseline contract.
// Successful phases also advance a durable sequence cursor so acceptance evidence
// must be collected in baseline -> settings -> search -> explorer -> monitor order.
WidgetRuntimeAcceptanceCode RunWidgetRuntimeAcceptanceProbe(
    std::wstring_view phase,
    std::wstring* reportPath = nullptr,
    std::wstring* failure = nullptr);

} // namespace turingdesk::desktop
