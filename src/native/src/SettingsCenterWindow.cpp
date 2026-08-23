#include "turingdesk/SettingsCenterWindow.h"

#include <shellapi.h>
#include <filesystem>
#include <iterator>

namespace fs = std::filesystem;

namespace turingdesk {
namespace {

constexpr wchar_t kDesktopSettingsClass[] = L"TuringDesk.Native.DesktopLibrary";

fs::path ModuleDirectory() {
    wchar_t modulePath[32768]{};
    const DWORD length = GetModuleFileNameW(nullptr, modulePath, static_cast<DWORD>(std::size(modulePath)));
    if (length == 0 || length >= std::size(modulePath)) return {};
    return fs::path(std::wstring(modulePath, length)).parent_path();
}

bool ActivateExistingDesktopSettings() {
    const HWND existing = FindWindowW(kDesktopSettingsClass, nullptr);
    if (!existing) return false;
    ShowWindow(existing, SW_SHOWNORMAL);
    SetForegroundWindow(existing);
    return true;
}

} // namespace

bool ShowSettingsCenterWindow(HINSTANCE, HWND owner, L3Agent&) {
    // Product baseline: the Wallpaper Engine-style desktop library is the one
    // primary Settings Center. Do not insert another launcher window in front
    // of it. AI, Harness, displays, rules and performance belong to that same
    // settings information architecture.
    if (ActivateExistingDesktopSettings()) return true;

    const fs::path directory = ModuleDirectory();
    if (directory.empty()) return false;
    const fs::path executable = directory / L"TuringDeskWallpaper.exe";
    std::error_code ec;
    if (!fs::is_regular_file(executable, ec)) return false;

    const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(
        owner, L"open", executable.c_str(), L"--settings", directory.c_str(), SW_SHOWNORMAL));
    return result > 32;
}

} // namespace turingdesk
