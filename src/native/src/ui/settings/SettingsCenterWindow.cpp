#include "miaodesk/SettingsCenterWindow.h"
#include "miaodesk/RuntimeLogger.h"

#include <shellapi.h>
#include <filesystem>
#include <iterator>

namespace fs = std::filesystem;

namespace miaodesk {
namespace {

constexpr wchar_t kDesktopSettingsClass[] = L"MiaoDesk.Native.DesktopLibrary";

fs::path ModuleDirectory() {
    wchar_t modulePath[32768]{};
    const DWORD length = GetModuleFileNameW(nullptr, modulePath, static_cast<DWORD>(std::size(modulePath)));
    if (length == 0 || length >= std::size(modulePath)) return {};
    return fs::path(std::wstring(modulePath, length)).parent_path();
}

bool ActivateExistingDesktopSettings() {
    const HWND existing = FindWindowW(kDesktopSettingsClass, nullptr);
    if (!existing) return false;
    miaodesk::log::Info(L"App.Settings", L"激活已存在的设置中心窗口");
    ShowWindow(existing, SW_SHOWNORMAL);
    SetForegroundWindow(existing);
    return true;
}

} // namespace

bool ShowSettingsCenterWindow(HINSTANCE, HWND owner, L3Agent&) {
    miaodesk::log::Info(L"App.Settings", L"请求打开设置中心");
    if (ActivateExistingDesktopSettings()) return true;

    const fs::path directory = ModuleDirectory();
    if (directory.empty()) return false;
    const fs::path executable = directory / L"MiaoDeskWallpaper.exe";
    std::error_code ec;
    if (!fs::is_regular_file(executable, ec)) {
        miaodesk::log::Error(L"App.Settings", L"找不到设置可执行文件: " + executable.wstring());
        return false;
    }

    miaodesk::log::Info(L"App.Settings", L"启动 MiaoDeskWallpaper.exe --settings");
    const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(
        owner, L"open", executable.c_str(), L"--settings", directory.c_str(), SW_SHOWNORMAL));
    return result > 32;
}

} // namespace miaodesk
