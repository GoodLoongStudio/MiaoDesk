#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>

#include "turingdesk/DesktopShellHost.h"
#include "turingdesk/DesktopWidgetStore.h"
#include "turingdesk/WallpaperLibrary.h"
#include "turingdesk/WallpaperPackage.h"
#include "turingdesk/WallpaperWebRuntimeCoordinator.h"
#include "turingdesk/WebDesktopSurfaceChild.h"
#include "turingdesk/WebWallpaperHost.h"

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

// Transitional lifecycle owner used while the legacy native wallpaper renderer
// and Web coordinator converge on DesktopShellHost. It now verifies both parent
// and shell-compatible layered composition rather than only repairing z-order.
class DesktopShellMaintenance {
public:
    DesktopShellMaintenance() = default;
    ~DesktopShellMaintenance() { Stop(); }

    void Start() {
        if (worker_.joinable()) return;
        stop_ = false;
        worker_ = std::thread([this] {
            while (!stop_) {
                std::wstring ignored;
                if (shell_.EnsureCurrent(&ignored)) {
                    const HWND parent = shell_.SurfaceParent();
                    if (parent && IsWindow(parent)) {
                        for (HWND child = GetWindow(parent, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
                            if (!IsDesktopSurfaceWindow(child)) continue;
                            const auto role = turingdesk::wallpaper::DesktopShellHost::InferRole(child);
                            const auto health = shell_.InspectSurface(child, role);
                            if (!health.layered || !health.childStyle)
                                shell_.PrepareSurface(child, true, nullptr);
                        }
                        shell_.RepairKnownTuringDeskSurfaces();
                    }
                }
                for (int i = 0; i < 5 && !stop_; ++i)
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });
    }

    void Stop() {
        stop_ = true;
        if (worker_.joinable()) worker_.join();
    }

private:
    std::atomic_bool stop_{false};
    std::thread worker_;
    turingdesk::wallpaper::DesktopShellHost shell_;
};

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR commandLine, int showCommand) {
    // Prefer the new layered desktop-surface WebView2 path. The legacy handler
    // remains directly below as a temporary migration fallback only.
    const int surfaceWebResult = turingdesk::wallpaper::TryRunWebDesktopSurfaceChild(instance);
    if (surfaceWebResult >= 0) return surfaceWebResult;
    const int legacyWebResult = turingdesk::wallpaper::TryRunWebWallpaperChild(instance);
    if (legacyWebResult >= 0) return legacyWebResult;

    const std::wstring_view args = commandLine ? std::wstring_view(commandLine) : std::wstring_view{};
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
    // already-running singleton. Do not let that short-lived process spawn a
    // duplicate set of Web wallpaper children.
    if (FindWindowW(kWallpaperControlClass, nullptr))
        return TuringDeskWallpaperMain(instance, previous, commandLine, showCommand);

    const HWINEVENTHOOK workAreaHook = InstallWorkAreaGuard();
    DesktopShellMaintenance shellMaintenance;
    shellMaintenance.Start();
    turingdesk::wallpaper::WallpaperWebRuntimeCoordinator webCoordinator;
    if (!webCoordinator.Start()) {
        shellMaintenance.Stop();
        if (workAreaHook) UnhookWinEvent(workAreaHook);
        return 43;
    }
    const int result = TuringDeskWallpaperMain(instance, previous, commandLine, showCommand);
    webCoordinator.Stop();
    shellMaintenance.Stop();
    if (workAreaHook) UnhookWinEvent(workAreaHook);
    return result;
}
