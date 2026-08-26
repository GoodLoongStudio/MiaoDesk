#include "turingdesk/WidgetRuntimeAcceptance.h"

#include <windows.h>

#include <iostream>
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

    const auto code = turingdesk::desktop::RunWidgetRuntimeAcceptanceProbe(
        phase, &report, &failure);

    if (!report.empty()) std::wcout << L"report=" << report << L"\n";
    if (!failure.empty()) std::wcerr << L"failure=" << failure << L"\n";
    if (code != turingdesk::desktop::WidgetRuntimeAcceptanceCode::Passed)
        return static_cast<int>(code);

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
