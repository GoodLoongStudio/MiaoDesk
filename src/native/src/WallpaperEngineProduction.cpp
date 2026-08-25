// Production bridge for the legacy WallpaperEngine implementation.
//
// The historical engine still contains combined persistence and desktop-shell
// code. Production routes performance policy through PerformanceUiAdapter ->
// PerformanceService, automation through AutomationUiAdapter -> AutomationService,
// and all Windows desktop attachment APIs through DesktopShellHost. The legacy
// engine source may still spell Progman/WorkerW/0x052C/SetParent while M2 is in
// progress, but those calls are intercepted here so there is only one effective
// production shell-discovery/re-parent/z-order implementation.
//
// Remove this bridge once WallpaperEngine.cpp no longer contains these legacy
// persistence/runtime ownership paths.

#include <windows.h>

#include "turingdesk/AutomationUiAdapter.h"
#include "turingdesk/DesktopShellHost.h"
#include "turingdesk/PerformanceUiAdapter.h"

#include <algorithm>
#include <cwchar>
#include <string>
#include <string_view>

namespace {

bool SameText(LPCWSTR left, std::wstring_view right) {
    return left && _wcsicmp(left, std::wstring(right).c_str()) == 0;
}

bool IsWindowClass(HWND window, std::wstring_view expected) {
    if (!window || !IsWindow(window)) return false;
    wchar_t className[160]{};
    return GetClassNameW(window, className, static_cast<int>(std::size(className))) > 0 &&
           _wcsicmp(className, std::wstring(expected).c_str()) == 0;
}

turingdesk::wallpaper::DesktopShellHost& ProductionShellHost() {
    static turingdesk::wallpaper::DesktopShellHost host;
    return host;
}

bool EnsureProductionShell(std::wstring* error = nullptr) {
    return ProductionShellHost().EnsureCurrent(error);
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
        turingdesk::wallpaper::PerformanceUiAdapter adapter;
        turingdesk::wallpaper::PerformanceConfig config;
        if (adapter.Load(&config)) {
            const auto value = PerformanceValue(config, key);
            if (!value.empty()) return static_cast<UINT>(_wtoi(value.c_str()));
        }
    }
    return ::GetPrivateProfileIntW(section, key, fallback, fileName);
}

DWORD WINAPI TuringDeskGetPrivateProfileStringW(
    LPCWSTR section, LPCWSTR key, LPCWSTR fallback, LPWSTR output, DWORD size, LPCWSTR fileName) {
    if (IsWallpaperSection(section) && IsPerformanceKey(key)) {
        turingdesk::wallpaper::PerformanceUiAdapter adapter;
        turingdesk::wallpaper::PerformanceConfig config;
        if (adapter.Load(&config)) {
            const auto value = PerformanceValue(config, key);
            if (!value.empty()) return CopyProfileValue(value, output, size);
        }
    }
    return ::GetPrivateProfileStringW(section, key, fallback, output, size, fileName);
}

BOOL WINAPI TuringDeskWritePrivateProfileStringW(
    LPCWSTR section, LPCWSTR key, LPCWSTR value, LPCWSTR fileName) {
    if (IsWallpaperSection(section) && IsPerformanceKey(key) && value) {
        turingdesk::wallpaper::PerformanceUiAdapter adapter;
        turingdesk::wallpaper::PerformanceConfig config;
        if (!adapter.Load(&config)) return FALSE;
        if (!ApplyPerformanceValue(&config, key, value)) return FALSE;
        return adapter.Save(config) ? TRUE : FALSE;
    }
    return ::WritePrivateProfileStringW(section, key, value, fileName);
}

HWND WINAPI TuringDeskFindWindowW(LPCWSTR className, LPCWSTR windowName) {
    if (SameText(className, L"Progman")) {
        std::wstring error;
        if (!EnsureProductionShell(&error)) return nullptr;
        return ProductionShellHost().Snapshot().progman;
    }
    return ::FindWindowW(className, windowName);
}

HWND WINAPI TuringDeskFindWindowExW(HWND parent, HWND childAfter, LPCWSTR className, LPCWSTR windowName) {
    if (SameText(className, L"WorkerW") || SameText(className, L"SHELLDLL_DefView")) {
        std::wstring error;
        if (!EnsureProductionShell(&error)) return nullptr;
        const auto& snapshot = ProductionShellHost().Snapshot();
        if (SameText(className, L"WorkerW")) return snapshot.workerW;
        return snapshot.shellDefView;
    }
    return ::FindWindowExW(parent, childAfter, className, windowName);
}

LRESULT WINAPI TuringDeskSendMessageTimeoutW(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
                                             UINT flags, UINT timeout, PDWORD_PTR result) {
    if (message == 0x052C) {
        std::wstring error;
        const bool ok = ProductionShellHost().Refresh(&error);
        if (result) *result = ok ? 1 : 0;
        return ok ? 1 : 0;
    }
    return ::SendMessageTimeoutW(window, message, wParam, lParam, flags, timeout, result);
}

HWND WINAPI TuringDeskSetParent(HWND child, HWND requestedParent) {
    if (IsWindowClass(child, L"TuringDesk.Native.WallpaperHost")) {
        auto& shell = ProductionShellHost();
        std::wstring error;
        if (!shell.EnsureCurrent(&error)) {
            SetLastError(ERROR_INVALID_WINDOW_HANDLE);
            return nullptr;
        }
        RECT bounds{};
        if (!GetWindowRect(child, &bounds) || bounds.right <= bounds.left || bounds.bottom <= bounds.top)
            bounds = RECT{0, 0, 1, 1};
        const HWND previous = GetParent(child);
        if (!shell.AttachSurface(child, turingdesk::wallpaper::DesktopSurfaceRole::Wallpaper,
                                 bounds, IsWindowVisible(child) != FALSE, &error)) {
            SetLastError(ERROR_INVALID_WINDOW_HANDLE);
            return nullptr;
        }
        SetLastError(ERROR_SUCCESS);
        return previous;
    }
    return ::SetParent(child, requestedParent);
}

BOOL WINAPI TuringDeskSetWindowPos(HWND window, HWND insertAfter, int x, int y, int width, int height, UINT flags) {
    if (IsWindowClass(window, L"WorkerW")) {
        std::wstring error;
        const bool ok = ProductionShellHost().RepairSurfaceStack(nullptr, &error);
        if (!ok) SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return ok ? TRUE : FALSE;
    }
    if (IsWindowClass(window, L"TuringDesk.Native.WallpaperHost")) {
        // Geometry remains a renderer concern during the transition, but parent
        // and z-order are not. Strip the caller's z-order intent, then let the
        // shared shell host restore the wallpaper/widget/icon stack.
        const UINT geometryFlags = flags | SWP_NOZORDER;
        if (!::SetWindowPos(window, nullptr, x, y, width, height, geometryFlags)) return FALSE;
        std::wstring error;
        if (!ProductionShellHost().RepairSurfaceStack(window, &error)) {
            SetLastError(ERROR_INVALID_WINDOW_HANDLE);
            return FALSE;
        }
        return TRUE;
    }
    return ::SetWindowPos(window, insertAfter, x, y, width, height, flags);
}

} // namespace

#define WallpaperAutomationStore AutomationUiAdapter
#define GetPrivateProfileIntW TuringDeskGetPrivateProfileIntW
#define GetPrivateProfileStringW TuringDeskGetPrivateProfileStringW
#define WritePrivateProfileStringW TuringDeskWritePrivateProfileStringW
#define FindWindowW TuringDeskFindWindowW
#define FindWindowExW TuringDeskFindWindowExW
#define SendMessageTimeoutW TuringDeskSendMessageTimeoutW
#define SetParent TuringDeskSetParent
#define SetWindowPos TuringDeskSetWindowPos
#include "WallpaperEngine.cpp"
#undef SetWindowPos
#undef SetParent
#undef SendMessageTimeoutW
#undef FindWindowExW
#undef FindWindowW
#undef WritePrivateProfileStringW
#undef GetPrivateProfileStringW
#undef GetPrivateProfileIntW
#undef WallpaperAutomationStore
