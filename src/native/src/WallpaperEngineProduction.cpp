// Production bridge for the legacy WallpaperEngine implementation.
//
// The historical engine still calls Win32 profile APIs for its combined config.
// In production, performance-policy keys are intercepted here and delegated to
// PerformanceService. Non-performance wallpaper/video keys keep their existing
// storage path until their domain migrations are completed.
//
// Remove this bridge once WallpaperEngine.cpp no longer contains direct
// performance-policy persistence.

#include <windows.h>

#include "turingdesk/PerformanceService.h"

#include <algorithm>
#include <cwchar>
#include <string>
#include <string_view>

namespace {

bool SameText(LPCWSTR left, std::wstring_view right) {
    return left && _wcsicmp(left, std::wstring(right).c_str()) == 0;
}

bool IsWallpaperSection(LPCWSTR section) {
    return SameText(section, L"Wallpaper");
}

bool IsPerformanceKey(LPCWSTR key) {
    if (!key) return false;
    for (const wchar_t* candidate : {
             L"PauseFullscreen",
             L"FpsCap",
             L"ThrottleFps",
             L"FullscreenAction",
             L"MaximizedAction",
             L"RemoteSessionAction",
             L"BatterySaverAction",
             L"LockedSessionAction",
             L"IdleAction",
             L"IdleThresholdSeconds"}) {
        if (_wcsicmp(key, candidate) == 0) return true;
    }
    return false;
}

DWORD CopyProfileValue(std::wstring_view value, LPWSTR output, DWORD size) {
    if (!output || size == 0) return 0;
    const std::size_t copyCount = std::min<std::size_t>(value.size(), static_cast<std::size_t>(size - 1));
    if (copyCount > 0) std::wmemcpy(output, value.data(), copyCount);
    output[copyCount] = L'\0';
    return static_cast<DWORD>(copyCount);
}

std::wstring PerformanceValue(const turingdesk::wallpaper::PerformanceConfig& config, LPCWSTR key) {
    using turingdesk::wallpaper::PerformanceActionKey;
    if (_wcsicmp(key, L"PauseFullscreen") == 0)
        return config.fullscreenAction == turingdesk::wallpaper::PerformanceAction::Pause ? L"1" : L"0";
    if (_wcsicmp(key, L"FpsCap") == 0) return std::to_wstring(config.fpsCap);
    if (_wcsicmp(key, L"ThrottleFps") == 0) return std::to_wstring(config.throttleFps);
    if (_wcsicmp(key, L"FullscreenAction") == 0) return PerformanceActionKey(config.fullscreenAction);
    if (_wcsicmp(key, L"MaximizedAction") == 0) return PerformanceActionKey(config.maximizedAction);
    if (_wcsicmp(key, L"RemoteSessionAction") == 0) return PerformanceActionKey(config.remoteSessionAction);
    if (_wcsicmp(key, L"BatterySaverAction") == 0) return PerformanceActionKey(config.batterySaverAction);
    if (_wcsicmp(key, L"LockedSessionAction") == 0) return PerformanceActionKey(config.lockedSessionAction);
    if (_wcsicmp(key, L"IdleAction") == 0) return PerformanceActionKey(config.idleAction);
    if (_wcsicmp(key, L"IdleThresholdSeconds") == 0) return std::to_wstring(config.idleThresholdSeconds);
    return {};
}

bool ApplyPerformanceValue(turingdesk::wallpaper::PerformanceConfig* config, LPCWSTR key, LPCWSTR value) {
    if (!config || !key || !value) return false;
    using turingdesk::wallpaper::ParsePerformanceAction;
    using turingdesk::wallpaper::PerformanceAction;

    if (_wcsicmp(key, L"PauseFullscreen") == 0) {
        config->fullscreenAction = _wtoi(value) != 0 ? PerformanceAction::Pause : PerformanceAction::Normal;
        return true;
    }
    if (_wcsicmp(key, L"FpsCap") == 0) {
        config->fpsCap = _wtoi(value);
        return true;
    }
    if (_wcsicmp(key, L"ThrottleFps") == 0) {
        config->throttleFps = _wtoi(value);
        return true;
    }
    if (_wcsicmp(key, L"FullscreenAction") == 0) {
        config->fullscreenAction = ParsePerformanceAction(value);
        return true;
    }
    if (_wcsicmp(key, L"MaximizedAction") == 0) {
        config->maximizedAction = ParsePerformanceAction(value);
        return true;
    }
    if (_wcsicmp(key, L"RemoteSessionAction") == 0) {
        config->remoteSessionAction = ParsePerformanceAction(value);
        return true;
    }
    if (_wcsicmp(key, L"BatterySaverAction") == 0) {
        config->batterySaverAction = ParsePerformanceAction(value);
        return true;
    }
    if (_wcsicmp(key, L"LockedSessionAction") == 0) {
        config->lockedSessionAction = ParsePerformanceAction(value);
        return true;
    }
    if (_wcsicmp(key, L"IdleAction") == 0) {
        config->idleAction = ParsePerformanceAction(value);
        return true;
    }
    if (_wcsicmp(key, L"IdleThresholdSeconds") == 0) {
        config->idleThresholdSeconds = static_cast<DWORD>(std::max(0, _wtoi(value)));
        return true;
    }
    return false;
}

UINT WINAPI TuringDeskGetPrivateProfileIntW(
    LPCWSTR section, LPCWSTR key, INT fallback, LPCWSTR fileName) {
    if (IsWallpaperSection(section) && IsPerformanceKey(key)) {
        turingdesk::desktop::PerformanceService service;
        turingdesk::wallpaper::PerformanceConfig config;
        if (service.GetConfig(&config).success) {
            const auto value = PerformanceValue(config, key);
            if (!value.empty()) return static_cast<UINT>(_wtoi(value.c_str()));
        }
    }
    return ::GetPrivateProfileIntW(section, key, fallback, fileName);
}

DWORD WINAPI TuringDeskGetPrivateProfileStringW(
    LPCWSTR section, LPCWSTR key, LPCWSTR fallback, LPWSTR output, DWORD size, LPCWSTR fileName) {
    if (IsWallpaperSection(section) && IsPerformanceKey(key)) {
        turingdesk::desktop::PerformanceService service;
        turingdesk::wallpaper::PerformanceConfig config;
        if (service.GetConfig(&config).success) {
            const auto value = PerformanceValue(config, key);
            if (!value.empty()) return CopyProfileValue(value, output, size);
        }
    }
    return ::GetPrivateProfileStringW(section, key, fallback, output, size, fileName);
}

BOOL WINAPI TuringDeskWritePrivateProfileStringW(
    LPCWSTR section, LPCWSTR key, LPCWSTR value, LPCWSTR fileName) {
    if (IsWallpaperSection(section) && IsPerformanceKey(key) && value) {
        turingdesk::desktop::PerformanceService service;
        turingdesk::wallpaper::PerformanceConfig config;
        if (!service.GetConfig(&config).success) return FALSE;
        if (!ApplyPerformanceValue(&config, key, value)) return FALSE;
        return service.SaveConfig(config).success ? TRUE : FALSE;
    }
    return ::WritePrivateProfileStringW(section, key, value, fileName);
}

} // namespace

#define GetPrivateProfileIntW TuringDeskGetPrivateProfileIntW
#define GetPrivateProfileStringW TuringDeskGetPrivateProfileStringW
#define WritePrivateProfileStringW TuringDeskWritePrivateProfileStringW
#include "WallpaperEngine.cpp"
#undef WritePrivateProfileStringW
#undef GetPrivateProfileStringW
#undef GetPrivateProfileIntW
