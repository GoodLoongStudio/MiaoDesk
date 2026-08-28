#include <windows.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include "turingdesk/DesktopShellHost.h"
#include "turingdesk/DesktopWidgetStore.h"
#include "turingdesk/WallpaperLibrary.h"
#include "turingdesk/WallpaperPackage.h"
#include "turingdesk/WallpaperWebRuntimeCoordinator.h"
#include "turingdesk/WebDesktopSurfaceChild.h"
#include "turingdesk/WebWallpaperHost.h"
#include "turingdesk/NativeWidgetHost.h"

namespace fs = std::filesystem;

// WallpaperEngine.cpp is compiled with its historical WinMain symbol renamed to
// TuringDeskWallpaperMain. The Windows headers declare wWinMain with C linkage,
// so the macro-renamed legacy entry keeps that linkage as well.
extern "C" int WINAPI TuringDeskWallpaperMain(HINSTANCE instance, HINSTANCE previous, PWSTR commandLine, int showCommand);

namespace {

constexpr wchar_t kWallpaperControlClass[] = L"TuringDesk.Native.WallpaperControl";
constexpr wchar_t kDesktopLibraryClass[] = L"TuringDesk.Native.DesktopLibrary";
constexpr wchar_t kWallpaperHostClass[] = L"TuringDesk.Native.WallpaperHost";
constexpr wchar_t kWebHostClass[] = L"TuringDesk.Native.WebWallpaperHost";

constexpr wchar_t kShellMode[] = L"--desktop-shell-supervisor";
constexpr wchar_t kWebRuntimeMode[] = L"--web-wallpaper-runtime";
constexpr wchar_t kWidgetRuntimeMode[] = L"--widget-runtime";

constexpr wchar_t kShellMutex[] = L"Local\\TuringDesk.DesktopShellSupervisor.v1";
constexpr wchar_t kWebRuntimeMutex[] = L"Local\\TuringDesk.WebWallpaperRuntime.v1";
constexpr wchar_t kWidgetRuntimeMutex[] = L"Local\\TuringDesk.WidgetRuntime.v1";
constexpr wchar_t kShellStopEvent[] = L"Local\\TuringDesk.DesktopShellSupervisor.Stop.v1";
constexpr wchar_t kWebRuntimeStopEvent[] = L"Local\\TuringDesk.WebWallpaperRuntime.Stop.v1";
constexpr wchar_t kWidgetRuntimeStopEvent[] = L"Local\\TuringDesk.WidgetRuntime.Stop.v1";

struct HelperSpec {
    const wchar_t* mode;
    const wchar_t* mutexName;
    const wchar_t* stopEventName;
};

constexpr HelperSpec kHelpers[] = {
    {kShellMode, kShellMutex, kShellStopEvent},
    {kWebRuntimeMode, kWebRuntimeMutex, kWebRuntimeStopEvent},
    {kWidgetRuntimeMode, kWidgetRuntimeMutex, kWidgetRuntimeStopEvent},
};

std::wstring ExecutablePath() {
    std::vector<wchar_t> buffer(32768);
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) return {};
    return std::wstring(buffer.data(), length);
}

std::wstring QuoteArg(std::wstring_view value) {
    std::wstring result = L"\"";
    unsigned backslashes = 0;
    for (wchar_t ch : value) {
        if (ch == L'\\') {
            ++backslashes;
            continue;
        }
        if (ch == L'\"') {
            result.append(backslashes * 2 + 1, L'\\');
            result.push_back(L'\"');
            backslashes = 0;
            continue;
        }
        result.append(backslashes, L'\\');
        backslashes = 0;
        result.push_back(ch);
    }
    result.append(backslashes * 2, L'\\');
    result.push_back(L'\"');
    return result;
}

bool NamedMutexExists(const wchar_t* name) {
    HANDLE mutex = OpenMutexW(SYNCHRONIZE, FALSE, name);
    if (!mutex) return false;
    CloseHandle(mutex);
    return true;
}

class SingletonGuard {
public:
    explicit SingletonGuard(const wchar_t* name) {
        handle_ = CreateMutexW(nullptr, FALSE, name);
        owner_ = handle_ && GetLastError() != ERROR_ALREADY_EXISTS;
    }
    ~SingletonGuard() {
        if (handle_) CloseHandle(handle_);
    }
    bool Owner() const noexcept { return owner_; }
private:
    HANDLE handle_{};
    bool owner_{};
};

bool LaunchHelper(const HelperSpec& helper) {
    if (NamedMutexExists(helper.mutexName)) return true;
    const std::wstring executable = ExecutablePath();
    if (executable.empty()) return false;

    std::wstring command = QuoteArg(executable) + L" " + helper.mode;
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const BOOL created = CreateProcessW(executable.c_str(), mutableCommand.data(), nullptr, nullptr, FALSE,
                                        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
                                        nullptr, nullptr, &startup, &process);
    if (!created) return false;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);

    const ULONGLONG deadline = GetTickCount64() + 3000;
    do {
        if (NamedMutexExists(helper.mutexName)) return true;
        Sleep(50);
    } while (GetTickCount64() < deadline);
    return NamedMutexExists(helper.mutexName);
}

void SignalHelperStop(const HelperSpec& helper) {
    HANDLE event = OpenEventW(EVENT_MODIFY_STATE, FALSE, helper.stopEventName);
    if (!event) return;
    SetEvent(event);
    CloseHandle(event);
}

HANDLE CreateStopEvent(const wchar_t* name) {
    return CreateEventW(nullptr, TRUE, FALSE, name);
}

std::wstring ReadProfileValue(const fs::path& path, const wchar_t* key) {
    wchar_t buffer[32768]{};
    GetPrivateProfileStringW(L"Wallpaper", key, L"", buffer, static_cast<DWORD>(std::size(buffer)), path.c_str());
    return buffer;
}

RECT ClampRectToWorkArea(RECT windowRect, const RECT& work) {
    const LONG workWidth = std::max<LONG>(1, work.right - work.left);
    const LONG workHeight = std::max<LONG>(1, work.bottom - work.top);
    LONG width = std::clamp<LONG>(windowRect.right - windowRect.left, 1, workWidth);
    LONG height = std::clamp<LONG>(windowRect.bottom - windowRect.top, 1, workHeight);
    LONG left = std::clamp<LONG>(windowRect.left, work.left, work.right - width);
    LONG top = std::clamp<LONG>(windowRect.top, work.top, work.bottom - height);
    return RECT{left, top, left + width, top + height};
}

bool IsDesktopLibraryWindow(HWND window) {
    if (!window || !IsWindow(window)) return false;
    wchar_t className[128]{};
    return GetClassNameW(window, className, static_cast<int>(std::size(className))) > 0 &&
           _wcsicmp(className, kDesktopLibraryClass) == 0;
}

void ClampDesktopLibraryToWorkArea(HWND window) {
    if (!IsDesktopLibraryWindow(window) || IsIconic(window)) return;
    const HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{sizeof(info)};
    RECT current{};
    if (!monitor || !GetMonitorInfoW(monitor, &info) || !GetWindowRect(window, &current)) return;
    const RECT bounded = ClampRectToWorkArea(current, info.rcWork);
    if (EqualRect(&current, &bounded)) return;
    SetWindowPos(window, nullptr, bounded.left, bounded.top,
                 bounded.right - bounded.left, bounded.bottom - bounded.top,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

void CALLBACK WorkAreaEventProc(HWINEVENTHOOK, DWORD event, HWND window,
                                LONG objectId, LONG childId, DWORD, DWORD) {
    if (event != EVENT_OBJECT_SHOW && event != EVENT_OBJECT_LOCATIONCHANGE) return;
    if (childId != CHILDID_SELF || (objectId != OBJID_WINDOW && objectId != OBJID_CLIENT)) return;
    ClampDesktopLibraryToWorkArea(window);
}

HWINEVENTHOOK InstallWorkAreaGuard() {
    return SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_LOCATIONCHANGE, nullptr, WorkAreaEventProc,
                           GetCurrentProcessId(), 0, WINEVENT_OUTOFCONTEXT);
}

bool WorkAreaSelfTest() {
    const RECT work{0, 0, 1920, 1040};
    const RECT tooLarge{-20, -10, 2000, 1100};
    const RECT clamped = ClampRectToWorkArea(tooLarge, work);
    if (clamped.left != 0 || clamped.top != 0 || clamped.right != 1920 || clamped.bottom != 1040) return false;
    const RECT normal{100, 100, 1200, 800};
    const RECT same = ClampRectToWorkArea(normal, work);
    return EqualRect(&normal, &same) != FALSE;
}

bool WebLibrarySelfTest() {
    std::error_code ec;
    const fs::path root = fs::temp_directory_path() /
        (L"TuringDesk-WebLibrary-SelfTest-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    fs::create_directories(root, ec);
    if (ec) return false;

    wchar_t previousLocalAppData[32768]{};
    const DWORD previousLocalAppDataLength = GetEnvironmentVariableW(
        L"LOCALAPPDATA", previousLocalAppData, static_cast<DWORD>(std::size(previousLocalAppData)));
    const bool hadLocalAppData = previousLocalAppDataLength > 0 && previousLocalAppDataLength < std::size(previousLocalAppData);
    const fs::path isolatedLocalAppData = root / L"LocalAppData";
    fs::create_directories(isolatedLocalAppData, ec);
    bool ok = !ec && SetEnvironmentVariableW(L"LOCALAPPDATA", isolatedLocalAppData.c_str()) != FALSE;

    turingdesk::wallpaper::WallpaperLibrary library(root / L"Library");
    std::wstring error;
    ok = ok && library.Load(&error);
    ok = ok && turingdesk::wallpaper::WallpaperLibrary::IsTrustedWebUrl(L"https://example.com/wallpaper");
    ok = ok && !turingdesk::wallpaper::WallpaperLibrary::IsTrustedWebUrl(L"http://example.com/wallpaper");
    ok = ok && !turingdesk::wallpaper::WallpaperLibrary::IsTrustedWebUrl(L"https://user:pass@example.com/wallpaper");

    const auto imported = library.ImportWebUrl(L"https://example.com/wallpaper", L"Web Self Test", &error);
    ok = ok && imported.has_value() && imported->kind == turingdesk::wallpaper::LibraryWallpaperKind::Web;

    turingdesk::wallpaper::WallpaperLibrary reloaded(root / L"Library");
    ok = ok && reloaded.Load(&error);
    if (imported) {
        const auto persisted = reloaded.Find(imported->id);
        ok = ok && persisted.has_value() && persisted->source.wstring() == L"https://example.com/wallpaper";
        ok = ok && turingdesk::wallpaper::ActivateWebWallpaperItem(*imported, L"", &error);

        const fs::path wallpaperConfig = isolatedLocalAppData / L"TuringDesk" / L"wallpaper.ini";
        ok = ok && ReadProfileValue(wallpaperConfig, L"Enabled") == L"1";
        ok = ok && _wcsicmp(ReadProfileValue(wallpaperConfig, L"Scene").c_str(), L"web") == 0;
        ok = ok && ReadProfileValue(wallpaperConfig, L"Image") == L"https://example.com/wallpaper";
        ok = ok && ReadProfileValue(wallpaperConfig, L"Video").empty();
    }

    if (hadLocalAppData)
        SetEnvironmentVariableW(L"LOCALAPPDATA", previousLocalAppData);
    else
        SetEnvironmentVariableW(L"LOCALAPPDATA", nullptr);

    fs::remove_all(root, ec);
    return ok;
}

bool IsDesktopSurfaceWindow(HWND window) {
    if (!window || !IsWindow(window)) return false;
    wchar_t className[160]{};
    if (!GetClassNameW(window, className, static_cast<int>(std::size(className)))) return false;
    return _wcsicmp(className, kWallpaperHostClass) == 0 || _wcsicmp(className, kWebHostClass) == 0;
}

std::vector<HWND> CollectDesktopSurfaceWindows() {
    std::vector<HWND> surfaces;
    EnumWindows([](HWND top, LPARAM raw) -> BOOL {
        auto* output = reinterpret_cast<std::vector<HWND>*>(raw);
        if (IsDesktopSurfaceWindow(top)) output->push_back(top);
        EnumChildWindows(top, [](HWND child, LPARAM childRaw) -> BOOL {
            auto* childOutput = reinterpret_cast<std::vector<HWND>*>(childRaw);
            if (IsDesktopSurfaceWindow(child)) childOutput->push_back(child);
            return TRUE;
        }, raw);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&surfaces));
    std::sort(surfaces.begin(), surfaces.end());
    surfaces.erase(std::unique(surfaces.begin(), surfaces.end()), surfaces.end());
    return surfaces;
}

int RunDesktopShellSupervisor() {
    SingletonGuard singleton(kShellMutex);
    if (!singleton.Owner()) return 0;
    HANDLE stopEvent = CreateStopEvent(kShellStopEvent);
    if (!stopEvent) return 45;

    turingdesk::wallpaper::DesktopShellHost shell;
    while (WaitForSingleObject(stopEvent, 250) == WAIT_TIMEOUT) {
        std::wstring ignored;
        if (!shell.EnsureCurrent(&ignored)) continue;
        for (HWND surface : CollectDesktopSurfaceWindows()) {
            if (!surface || !IsWindow(surface)) continue;
            const auto role = turingdesk::wallpaper::DesktopShellHost::InferRole(surface);
            auto health = shell.InspectSurface(surface, role);
            RECT screenRect{};
            if (!GetWindowRect(surface, &screenRect) ||
                screenRect.right <= screenRect.left || screenRect.bottom <= screenRect.top) continue;

            if (!health.parent || !health.childStyle || !health.layered || !health.geometry) {
                shell.AttachSurface(surface, role, screenRect,
                                    IsWindowVisible(surface) != FALSE, nullptr);
            } else {
                shell.PrepareSurface(surface, role != turingdesk::wallpaper::DesktopSurfaceRole::Widget, nullptr);
            }
        }
        shell.RepairKnownTuringDeskSurfaces();
    }

    CloseHandle(stopEvent);
    return 0;
}

int RunScopedWebCoordinator(turingdesk::wallpaper::WallpaperWebRuntimeScope scope,
                            const wchar_t* mutexName, const wchar_t* stopEventName) {
    SingletonGuard singleton(mutexName);
    if (!singleton.Owner()) return 0;
    HANDLE stopEvent = CreateStopEvent(stopEventName);
    if (!stopEvent) return 46;

    turingdesk::wallpaper::WallpaperWebRuntimeCoordinator coordinator(scope);
    if (!coordinator.Start()) {
        CloseHandle(stopEvent);
        return 47;
    }
    while (WaitForSingleObject(stopEvent, 500) == WAIT_TIMEOUT) {}
    coordinator.Stop();
    CloseHandle(stopEvent);
    return 0;
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR commandLine, int showCommand) {
    // There is one production WebView2 desktop-surface child implementation.
    // The old WebWallpaperChildHost remains compiled only as migration source
    // reference and is no longer routed from the executable entrypoint.
    const int surfaceWebResult = turingdesk::wallpaper::TryRunWebDesktopSurfaceChild(instance);
    if (surfaceWebResult >= 0) return surfaceWebResult;

    const int nativeWidgetResult = turingdesk::wallpaper::TryRunNativeWidgetHost(instance);
    if (nativeWidgetResult >= 0) return nativeWidgetResult;

    const std::wstring_view args = commandLine ? std::wstring_view(commandLine) : std::wstring_view{};
    if (args.find(kShellMode) != std::wstring_view::npos)
        return RunDesktopShellSupervisor();
    if (args.find(kWebRuntimeMode) != std::wstring_view::npos)
        return RunScopedWebCoordinator(turingdesk::wallpaper::WallpaperWebRuntimeScope::WebWallpaper,
                                       kWebRuntimeMutex, kWebRuntimeStopEvent);
    if (args.find(kWidgetRuntimeMode) != std::wstring_view::npos)
        return RunScopedWebCoordinator(turingdesk::wallpaper::WallpaperWebRuntimeScope::Widgets,
                                       kWidgetRuntimeMutex, kWidgetRuntimeStopEvent);

    const bool selfTest = args.find(L"--self-test") != std::wstring_view::npos;
    if (selfTest) {
        if (!turingdesk::wallpaper::WebWallpaperProcessSet::SelfTest()) return 37;
        if (!WebLibrarySelfTest()) return 38;
        if (!turingdesk::wallpaper::WallpaperWebRuntimeCoordinator::SelfTest()) return 39;
        if (!WorkAreaSelfTest()) return 40;
        if (!turingdesk::wallpaper::WallpaperPackage::SelfTest()) return 41;
        if (!turingdesk::wallpaper::DesktopWidgetStore::SelfTest()) return 42;
        if (!turingdesk::wallpaper::DesktopShellHost::SelfTest()) return 44;
        return TuringDeskWallpaperMain(instance, previous, commandLine, showCommand);
    }

    // A second TuringDeskWallpaper invocation is only a command sender for the
    // already-running native wallpaper singleton. It must not own or stop the
    // shared helper fault domains.
    if (FindWindowW(kWallpaperControlClass, nullptr))
        return TuringDeskWallpaperMain(instance, previous, commandLine, showCommand);

    for (const auto& helper : kHelpers) LaunchHelper(helper);

    const HWINEVENTHOOK workAreaHook = InstallWorkAreaGuard();
    const int result = TuringDeskWallpaperMain(instance, previous, commandLine, showCommand);

    // This path is reached on an orderly native wallpaper shutdown. An abnormal
    // crash never sends these events, so Shell/Web/Widget helpers survive long
    // enough for the next native runtime instance to reconnect to them.
    for (const auto& helper : kHelpers) SignalHelperStop(helper);
    if (workAreaHook) UnhookWinEvent(workAreaHook);
    return result;
}
