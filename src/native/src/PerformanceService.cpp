#include "turingdesk/PerformanceService.h"

#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <iterator>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace turingdesk::desktop {
namespace {

fs::path ConfigPath() {
    wchar_t local[32768]{};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", local, static_cast<DWORD>(std::size(local)));
    fs::path directory = (length > 0 && length < std::size(local))
        ? fs::path(local) / L"TuringDesk"
        : fs::temp_directory_path() / L"TuringDesk";
    std::error_code ec;
    fs::create_directories(directory, ec);
    return directory / L"wallpaper.ini";
}

std::wstring ReadText(const fs::path& path, const wchar_t* key, const wchar_t* fallback) {
    std::vector<wchar_t> value(256);
    GetPrivateProfileStringW(L"Wallpaper", key, fallback, value.data(),
                             static_cast<DWORD>(value.size()), path.c_str());
    return value.data();
}

bool WriteText(const fs::path& path, const wchar_t* key, const std::wstring& value) {
    return WritePrivateProfileStringW(L"Wallpaper", key, value.c_str(), path.c_str()) != FALSE;
}

bool WriteAction(const fs::path& path, const wchar_t* key, wallpaper::PerformanceAction action) {
    return WritePrivateProfileStringW(L"Wallpaper", key, wallpaper::PerformanceActionKey(action), path.c_str()) != FALSE;
}

} // namespace

PerformanceServiceResult PerformanceService::GetConfig(wallpaper::PerformanceConfig* config) const {
    if (!config) return {false, L"PerformanceConfig 输出不能为空。"};
    const fs::path path = ConfigPath();

    wallpaper::PerformanceConfig next;
    next.fpsCap = wallpaper::NormalizeFpsCap(
        static_cast<int>(GetPrivateProfileIntW(L"Wallpaper", L"FpsCap", 30, path.c_str())));
    next.throttleFps = wallpaper::NormalizeFpsCap(
        static_cast<int>(GetPrivateProfileIntW(L"Wallpaper", L"ThrottleFps", 15, path.c_str())));
    next.fullscreenAction = wallpaper::ParsePerformanceAction(ReadText(path, L"FullscreenAction", L"pause"));
    next.maximizedAction = wallpaper::ParsePerformanceAction(ReadText(path, L"MaximizedAction", L"throttle"));
    next.remoteSessionAction = wallpaper::ParsePerformanceAction(ReadText(path, L"RemoteSessionAction", L"throttle"));
    next.batterySaverAction = wallpaper::ParsePerformanceAction(ReadText(path, L"BatterySaverAction", L"throttle"));
    next.lockedSessionAction = wallpaper::ParsePerformanceAction(ReadText(path, L"LockedSessionAction", L"stop"));
    next.idleAction = wallpaper::ParsePerformanceAction(ReadText(path, L"IdleAction", L"throttle"));
    const int idleSeconds = static_cast<int>(
        GetPrivateProfileIntW(L"Wallpaper", L"IdleThresholdSeconds", 120, path.c_str()));
    next.idleThresholdSeconds = static_cast<DWORD>(std::clamp(idleSeconds, 30, 3600));

    *config = next;
    return {true, L"性能策略读取完成。"};
}

PerformanceServiceResult PerformanceService::SaveConfig(const wallpaper::PerformanceConfig& config) const {
    const fs::path path = ConfigPath();
    bool ok = true;
    ok = WriteText(path, L"FpsCap", std::to_wstring(wallpaper::NormalizeFpsCap(config.fpsCap))) && ok;
    ok = WriteText(path, L"ThrottleFps", std::to_wstring(wallpaper::NormalizeFpsCap(config.throttleFps))) && ok;
    ok = WriteAction(path, L"FullscreenAction", config.fullscreenAction) && ok;
    ok = WriteAction(path, L"MaximizedAction", config.maximizedAction) && ok;
    ok = WriteAction(path, L"RemoteSessionAction", config.remoteSessionAction) && ok;
    ok = WriteAction(path, L"BatterySaverAction", config.batterySaverAction) && ok;
    ok = WriteAction(path, L"LockedSessionAction", config.lockedSessionAction) && ok;
    ok = WriteAction(path, L"IdleAction", config.idleAction) && ok;
    const DWORD idleSeconds = std::clamp<DWORD>(config.idleThresholdSeconds, 30, 3600);
    ok = WriteText(path, L"IdleThresholdSeconds", std::to_wstring(idleSeconds)) && ok;
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, path.c_str());

    return ok ? PerformanceServiceResult{true, L"性能策略已保存。"}
              : PerformanceServiceResult{false, L"无法保存性能策略。"};
}

} // namespace turingdesk::desktop
