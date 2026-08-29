// Production bridge for the legacy WallpaperEngine implementation.
//
// Desktop attachment is now owned directly by DesktopShellHost from
// WallpaperEngine.cpp. This bridge remains only for the M1 migration adapters:
// performance policy routes through PerformanceUiAdapter -> PerformanceService
// and automation routes through AutomationUiAdapter -> AutomationService.
//
// Remove this bridge completely once the remaining persistence compatibility
// macros are no longer required by the legacy UI implementation.

#include <windows.h>
#include <shellapi.h>

#include "miaodesk/AutomationUiAdapter.h"
#include "miaodesk/DesktopAiSettingsPage.h"
#include "miaodesk/PerformanceUiAdapter.h"

#include <algorithm>
#include <cwchar>
#include <string>
#include <string_view>

namespace {

BOOL WINAPI MiaoDeskWallpaperShellNotifyIconW(DWORD, PNOTIFYICONDATAW) {
    // Production has exactly one tray owner: the main 妙喵 process.
    // Historical wallpaper-host tray calls become successful no-ops.
    return TRUE;
}

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

std::wstring PerformanceValue(const miaodesk::wallpaper::PerformanceConfig& config, LPCWSTR key) {
    using miaodesk::wallpaper::PerformanceActionKey;
    if (_wcsicmp(key, L"PauseFullscreen") == 0)
        return config.fullscreenAction == miaodesk::wallpaper::PerformanceAction::Pause ? L"1" : L"0";
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

bool ApplyPerformanceValue(miaodesk::wallpaper::PerformanceConfig* config, LPCWSTR key, LPCWSTR value) {
    if (!config || !key || !value) return false;
    using miaodesk::wallpaper::ParsePerformanceAction;
    using miaodesk::wallpaper::PerformanceAction;

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

UINT WINAPI MiaoDeskGetPrivateProfileIntW(
    LPCWSTR section, LPCWSTR key, INT fallback, LPCWSTR fileName) {
    if (IsWallpaperSection(section) && IsPerformanceKey(key)) {
        miaodesk::wallpaper::PerformanceUiAdapter adapter;
        miaodesk::wallpaper::PerformanceConfig config;
        if (adapter.Load(&config)) {
            const auto value = PerformanceValue(config, key);
            if (!value.empty()) return static_cast<UINT>(_wtoi(value.c_str()));
        }
    }
    return ::GetPrivateProfileIntW(section, key, fallback, fileName);
}

DWORD WINAPI MiaoDeskGetPrivateProfileStringW(
    LPCWSTR section, LPCWSTR key, LPCWSTR fallback, LPWSTR output, DWORD size, LPCWSTR fileName) {
    if (IsWallpaperSection(section) && IsPerformanceKey(key)) {
        miaodesk::wallpaper::PerformanceUiAdapter adapter;
        miaodesk::wallpaper::PerformanceConfig config;
        if (adapter.Load(&config)) {
            const auto value = PerformanceValue(config, key);
            if (!value.empty()) return CopyProfileValue(value, output, size);
        }
    }
    return ::GetPrivateProfileStringW(section, key, fallback, output, size, fileName);
}

BOOL WINAPI MiaoDeskWritePrivateProfileStringW(
    LPCWSTR section, LPCWSTR key, LPCWSTR value, LPCWSTR fileName) {
    if (IsWallpaperSection(section) && IsPerformanceKey(key) && value) {
        miaodesk::wallpaper::PerformanceUiAdapter adapter;
        miaodesk::wallpaper::PerformanceConfig config;
        if (!adapter.Load(&config)) return FALSE;
        if (!ApplyPerformanceValue(&config, key, value)) return FALSE;
        return adapter.Save(config) ? TRUE : FALSE;
    }
    return ::WritePrivateProfileStringW(section, key, value, fileName);
}

int WINAPI MiaoDeskMessageBoxW(HWND owner, LPCWSTR text, LPCWSTR caption, UINT type) {
    // The V2 shell still emits this one historical placeholder callback for its
    // AI navigation item. Convert only that placeholder into the real in-place
    // API/Harness page; all other product dialogs retain normal MessageBoxW behavior.
    if (text && std::wstring_view(text).find(L"AI 模型配置位于 MiaoDesk 设置中心") != std::wstring_view::npos) {
        return miaodesk::wallpaper::ShowDesktopAiSettingsPage(owner) ? IDOK : IDCANCEL;
    }
    return ::MessageBoxW(owner, text, caption, type);
}

} // namespace

// WallpaperEngine.cpp is still included as a migration implementation unit and
// historically refers to the desktop namespace as wallpaper::. Keep that alias
// local to this production bridge instead of leaking it into public headers.
namespace wallpaper = miaodesk::wallpaper;

#define WallpaperAutomationStore AutomationUiAdapter
#define GetPrivateProfileIntW MiaoDeskGetPrivateProfileIntW
#define GetPrivateProfileStringW MiaoDeskGetPrivateProfileStringW
#define WritePrivateProfileStringW MiaoDeskWritePrivateProfileStringW
#define MessageBoxW MiaoDeskMessageBoxW
#define Shell_NotifyIconW MiaoDeskWallpaperShellNotifyIconW
#include "WallpaperEngine.cpp"
#undef Shell_NotifyIconW
#undef MessageBoxW
#undef WritePrivateProfileStringW
#undef GetPrivateProfileStringW
#undef GetPrivateProfileIntW
#undef WallpaperAutomationStore
