#include "turingdesk/WidgetRuntimeAcceptance.h"

#include <windows.h>

#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

namespace {

std::wstring_view ReadPhase(int argc, wchar_t** argv) {
    constexpr std::wstring_view prefix = L"--phase=";
    for (int i = 1; i < argc; ++i) {
        const std::wstring_view arg = argv[i] ? std::wstring_view(argv[i]) : std::wstring_view{};
        if (arg.starts_with(prefix)) return arg.substr(prefix.size());
    }
    return L"baseline";
}

struct PhaseContextExpectation {
    const wchar_t* windowClass = nullptr;
    const wchar_t* description = nullptr;
};

PhaseContextExpectation ExpectedPhaseContext(std::wstring_view phase) {
    if (phase == L"settings") {
        return {L"TuringDesk.Native.DesktopLibrary", L"TuringDesk desktop library/settings window"};
    }
    if (phase == L"search") {
        return {L"TuringDesk.Native.SearchWindow", L"TuringDesk search window"};
    }
    return {};
}

struct PhaseContextSearch {
    const wchar_t* expectedClass = nullptr;
    DWORD sessionId = 0;
    bool found = false;
};

BOOL CALLBACK FindVisiblePhaseContextWindow(HWND hwnd, LPARAM parameter) {
    auto* search = reinterpret_cast<PhaseContextSearch*>(parameter);
    if (!search || search->found || !IsWindowVisible(hwnd) || GetAncestor(hwnd, GA_ROOT) != hwnd) return TRUE;

    wchar_t className[256]{};
    if (GetClassNameW(hwnd, className, static_cast<int>(std::size(className))) <= 0) return TRUE;
    if (std::wstring_view(className) != search->expectedClass) return TRUE;

    DWORD processId = 0;
    GetWindowThreadProcessId(hwnd, &processId);
    if (processId == 0) return TRUE;

    DWORD windowSessionId = 0;
    if (!ProcessIdToSessionId(processId, &windowSessionId) || windowSessionId != search->sessionId) return TRUE;

    search->found = true;
    return FALSE;
}

bool PhaseContextReady(std::wstring_view phase, std::wstring* failure) {
    const auto expected = ExpectedPhaseContext(phase);
    if (!expected.windowClass) return true;

    DWORD sessionId = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &sessionId)) {
        if (failure) *failure = L"无法解析 M3 acceptance 进程的 Windows session。";
        return false;
    }

    PhaseContextSearch search{expected.windowClass, sessionId, false};
    EnumWindows(FindVisiblePhaseContextWindow, reinterpret_cast<LPARAM>(&search));
    if (search.found) return true;

    if (failure) {
        *failure = L"M3 " + std::wstring(phase) + L" 阶段缺少同一 Windows session 内可见的 "
            + expected.description + L"（class=" + expected.windowClass
            + L"）。健康取样不能脱离要求的产品窗口上下文。";
    }
    return false;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    const std::wstring_view phase = ReadPhase(argc, argv);
    const bool baseline = phase == L"baseline";
    std::wstring report;
    std::wstring failure;

    // Later phases validate persisted placement configuration before the runtime
    // probe can advance the durable sequence cursor. PID/HWND identity is not
    // part of this checkpoint because Explorer recovery may recreate surfaces.
    if (!baseline) {
        const auto configCode = turingdesk::desktop::CheckWidgetAcceptanceConfigContinuity(
            phase, false, &failure);
        if (configCode != turingdesk::desktop::WidgetRuntimeAcceptanceCode::Passed) {
            if (!failure.empty()) std::wcerr << L"failure=" << failure << L"\n";
            return static_cast<int>(configCode);
        }
    }

    // Settings/Search acceptance only means something while the requested product
    // surface is visibly present. Validate immediately before and after the health
    // sample so a wrapper-level foreground observation cannot race with the probe.
    if (!PhaseContextReady(phase, &failure)) {
        if (!failure.empty()) std::wcerr << L"failure=" << failure << L"\n";
        return static_cast<int>(turingdesk::desktop::WidgetRuntimeAcceptanceCode::SurfaceUnhealthy);
    }

    const auto code = turingdesk::desktop::RunWidgetRuntimeAcceptanceProbe(
        phase, &report, &failure);

    if (!report.empty()) std::wcout << L"report=" << report << L"\n";
    if (!failure.empty()) std::wcerr << L"failure=" << failure << L"\n";
    if (code != turingdesk::desktop::WidgetRuntimeAcceptanceCode::Passed)
        return static_cast<int>(code);

    failure.clear();
    if (!PhaseContextReady(phase, &failure)) {
        if (!failure.empty()) std::wcerr << L"failure=" << failure << L"\n";
        return static_cast<int>(turingdesk::desktop::WidgetRuntimeAcceptanceCode::SurfaceUnhealthy);
    }

    // Only a successful baseline runtime probe is allowed to establish the
    // placement config checkpoint. If writing it fails, a new baseline is required.
    if (baseline) {
        failure.clear();
        const auto configCode = turingdesk::desktop::CheckWidgetAcceptanceConfigContinuity(
            phase, true, &failure);
        if (configCode != turingdesk::desktop::WidgetRuntimeAcceptanceCode::Passed) {
            if (!failure.empty()) std::wcerr << L"failure=" << failure << L"\n";
            return static_cast<int>(configCode);
        }
    }

    std::wcout << L"M3 Widget runtime acceptance probe passed.\n";
    return static_cast<int>(turingdesk::desktop::WidgetRuntimeAcceptanceCode::Passed);
}
