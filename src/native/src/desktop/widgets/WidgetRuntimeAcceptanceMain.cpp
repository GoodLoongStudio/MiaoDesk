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
    std::wstring report;
    std::wstring failure;
    const auto code = turingdesk::desktop::RunWidgetRuntimeAcceptanceProbe(
        ReadPhase(argc, argv), &report, &failure);

    if (!report.empty()) std::wcout << L"report=" << report << L"\n";
    if (!failure.empty()) std::wcerr << L"failure=" << failure << L"\n";
    if (code == turingdesk::desktop::WidgetRuntimeAcceptanceCode::Passed)
        std::wcout << L"M3 Widget runtime acceptance probe passed.\n";
    return static_cast<int>(code);
}
