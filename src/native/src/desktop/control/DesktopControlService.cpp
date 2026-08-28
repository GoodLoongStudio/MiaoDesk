#include "turingdesk/DesktopControlService.h"

#include "turingdesk/WallpaperRuntimeControl.h"

#include <windows.h>
#include <shellapi.h>

#include <filesystem>
#include <iterator>
#include <system_error>

namespace fs = std::filesystem;

namespace turingdesk::desktop {
namespace {

constexpr wchar_t kWallpaperControlClass[] = L"TuringDesk.Native.WallpaperControl";
constexpr wchar_t kShellMutex[] = L"Local\\TuringDesk.DesktopShellSupervisor.v1";
constexpr wchar_t kWebRuntimeMutex[] = L"Local\\TuringDesk.WebWallpaperRuntime.v1";
constexpr wchar_t kWidgetRuntimeMutex[] = L"Local\\TuringDesk.WidgetRuntime.v1";
constexpr wchar_t kShellMode[] = L"--desktop-shell-supervisor";
constexpr wchar_t kWebRuntimeMode[] = L"--web-wallpaper-runtime";
constexpr wchar_t kWidgetRuntimeMode[] = L"--widget-runtime";
constexpr DWORD kRuntimeReadyTimeoutMs = 5000;
constexpr DWORD kRuntimeReadyPollMs = 100;

struct RuntimeHelper {
    const wchar_t* mutexName;
    const wchar_t* mode;
};

constexpr RuntimeHelper kRuntimeHelpers[] = {
    {kShellMutex, kShellMode},
    {kWebRuntimeMutex, kWebRuntimeMode},
    {kWidgetRuntimeMutex, kWidgetRuntimeMode},
};

fs::path ModuleDirectory() {
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) return {};
    path.resize(length);
    return fs::path(path).parent_path();
}

bool NamedMutexExists(const wchar_t* name) {
    HANDLE mutex = OpenMutexW(SYNCHRONIZE, FALSE, name);
    if (!mutex) return false;
    CloseHandle(mutex);
    return true;
}

bool RuntimeInfrastructureReady() {
    if (!FindWindowW(kWallpaperControlClass, nullptr)) return false;
    for (const auto& helper : kRuntimeHelpers)
        if (!NamedMutexExists(helper.mutexName)) return false;
    return true;
}

bool WaitForRuntimeControl() {
    const ULONGLONG deadline = GetTickCount64() + kRuntimeReadyTimeoutMs;
    do {
        if (RuntimeInfrastructureReady()) return true;
        Sleep(kRuntimeReadyPollMs);
    } while (GetTickCount64() < deadline);
    return RuntimeInfrastructureReady();
}

bool LaunchRuntime(const fs::path& executable, const wchar_t* arguments) {
    const HINSTANCE launched = ShellExecuteW(nullptr, L"open", executable.c_str(), arguments,
                                             executable.parent_path().c_str(), SW_SHOWNOACTIVATE);
    return reinterpret_cast<INT_PTR>(launched) > 32;
}

bool RepairMissingHelpers(const fs::path& executable) {
    bool launched = false;
    for (const auto& helper : kRuntimeHelpers) {
        if (NamedMutexExists(helper.mutexName)) continue;
        launched = LaunchRuntime(executable, helper.mode) || launched;
    }
    return launched;
}

DesktopControlResult FromWallpaper(WallpaperServiceResult result) {
    return {result.success, result.message};
}

DesktopControlResult FromWidget(WidgetServiceResult result) {
    return {result.success, result.message};
}

} // namespace

DesktopControlResult DesktopControlService::EnsureRuntime() const {
    if (RuntimeInfrastructureReady())
        return {true, L"桌面运行时与隔离 Shell/Web/Widget helper 已就绪。"};

    const fs::path executable = ModuleDirectory() / L"TuringDeskWallpaper.exe";
    std::error_code ec;
    if (!fs::exists(executable, ec) || !fs::is_regular_file(executable, ec))
        return {false, L"找不到 TuringDeskWallpaper.exe。"};

    const bool nativeAlive = FindWindowW(kWallpaperControlClass, nullptr) != nullptr;
    bool launched = false;
    if (nativeAlive) {
        // Repair only the failed fault domain. This avoids restarting Native
        // Wallpaper, Pi, Harness, Search, or healthy desktop helpers.
        launched = RepairMissingHelpers(executable);
    } else {
        // The primary wallpaper process is responsible for starting all helper
        // domains on a cold boot.
        launched = LaunchRuntime(executable, nullptr);
    }

    if (!launched && !RuntimeInfrastructureReady())
        return {false, L"无法启动或修复桌面运行时。"};
    if (!WaitForRuntimeControl())
        return {false, L"桌面运行时进程已启动，但 Native/Shell/Web/Widget 故障域在 5 秒内没有全部就绪。请查看 TuringDesk-Logs。"};
    return {true, L"桌面运行时与隔离 Shell/Web/Widget helper 已启动并就绪。"};
}

DesktopControlResult DesktopControlService::GetSnapshot(DesktopSnapshot* snapshot) const {
    if (!snapshot) return {false, L"DesktopSnapshot 输出不能为空。"};

    WallpaperService wallpaperService;
    WallpaperState wallpaperState;
    const auto wallpaperResult = wallpaperService.GetState(&wallpaperState);
    if (!wallpaperResult.success) return FromWallpaper(wallpaperResult);

    WidgetService widgetService;
    std::vector<wallpaper::DesktopWidget> widgets;
    const auto widgetResult = widgetService.List(&widgets);
    if (!widgetResult.success) return FromWidget(widgetResult);

    WidgetRuntimeHealth widgetRuntime;
    const auto widgetRuntimeResult = widgetService.GetRuntimeHealth(&widgetRuntime);
    if (!widgetRuntimeResult.success) return FromWidget(widgetRuntimeResult);

    snapshot->desktop.enabled = wallpaperState.enabled;
    snapshot->desktop.scene = wallpaperState.scene;
    snapshot->desktop.layout = wallpaperState.layout;
    snapshot->desktop.scale = wallpaperState.scale;
    snapshot->desktop.fpsCap = wallpaperState.fpsCap;
    snapshot->desktop.imageOrWebSource = wallpaperState.imageOrWebSource;
    snapshot->desktop.videoSource = wallpaperState.videoSource;
    snapshot->desktop.widgetCount = widgets.size();
    snapshot->widgets = std::move(widgets);
    snapshot->widgetRuntime = std::move(widgetRuntime);
    return {true, L"统一桌面快照读取完成。"};
}

DesktopControlResult DesktopControlService::GetState(DesktopState* state) const {
    if (!state) return {false, L"DesktopState 输出不能为空。"};
    DesktopSnapshot snapshot;
    const auto result = GetSnapshot(&snapshot);
    if (!result.success) return result;
    *state = std::move(snapshot.desktop);
    return {true, L"桌面状态读取完成。"};
}

DesktopControlResult DesktopControlService::ApplyWebPackage(const fs::path& package) const {
    WallpaperService service;
    const auto result = service.ApplyWebPackage(package);
    if (!result.success) return FromWallpaper(result);
    const auto runtime = EnsureRuntime();
    if (!runtime.success) return runtime;
    return {true, result.message};
}

DesktopControlResult DesktopControlService::ApplyLibraryItem(const wallpaper::WallpaperLibraryItem& item) const {
    WallpaperService service;
    const auto result = service.ApplyLibraryItem(item);
    if (!result.success) return FromWallpaper(result);
    const auto runtime = EnsureRuntime();
    if (!runtime.success) return runtime;
    return {true, result.message};
}

DesktopControlResult DesktopControlService::AssignLibraryItemToMonitor(
    const wallpaper::WallpaperLibraryItem& item,
    std::wstring_view monitorId,
    std::wstring_view friendlyName) const {
    WallpaperService service;
    const auto result = service.AssignLibraryItemToMonitor(item, monitorId, friendlyName);
    if (!result.success) return FromWallpaper(result);
    const auto runtime = EnsureRuntime();
    if (!runtime.success) return runtime;
    return {true, result.message};
}

DesktopControlResult DesktopControlService::ClearMonitorAssignment(std::wstring_view monitorId) const {
    WallpaperService service;
    const auto result = service.ClearMonitorAssignment(monitorId);
    if (!result.success) return FromWallpaper(result);
    const auto runtime = EnsureRuntime();
    if (!runtime.success) return runtime;
    return {true, result.message};
}

DesktopControlResult DesktopControlService::CreateWebWidget(
    const WebWidgetCreateRequest& request,
    wallpaper::DesktopWidget* created) const {
    WidgetService service;
    const auto result = service.CreateWeb(request, created);
    if (!result.success) return FromWidget(result);
    const auto runtime = EnsureRuntime();
    if (!runtime.success) return runtime;
    return {true, result.message};
}

DesktopControlResult DesktopControlService::CreateNativeWidget(
    const NativeWidgetCreateRequest& request,
    wallpaper::DesktopWidget* created) const {
    WidgetService service;
    const auto result = service.CreateNative(request, created);
    if (!result.success) return FromWidget(result);
    const auto runtime = EnsureRuntime();
    if (!runtime.success) return runtime;
    return {true, result.message};
}

DesktopControlResult DesktopControlService::UpdateWidget(const WidgetUpdateRequest& request) const {
    WidgetService service;
    const auto result = service.Update(request);
    if (!result.success) return FromWidget(result);
    const auto runtime = EnsureRuntime();
    if (!runtime.success) return runtime;
    return {true, result.message};
}

DesktopControlResult DesktopControlService::RemoveWidget(std::wstring_view id) const {
    WidgetService service;
    return FromWidget(service.Remove(id));
}

DesktopControlResult DesktopControlService::ListWidgets(std::vector<wallpaper::DesktopWidget>* widgets) const {
    WidgetService service;
    return FromWidget(service.List(widgets));
}

DesktopControlResult DesktopControlService::FindWidget(std::wstring_view id, wallpaper::DesktopWidget* widget) const {
    WidgetService service;
    return FromWidget(service.Find(id, widget));
}

DesktopControlResult DesktopControlService::SetWallpaperEnabled(const bool enabled) const {
    WallpaperService service;
    const auto persisted = service.SetEnabled(enabled);
    if (!persisted.success) return FromWallpaper(persisted);

    const fs::path executable = ModuleDirectory() / L"TuringDeskWallpaper.exe";
    std::error_code ec;
    if (!fs::exists(executable, ec) || !fs::is_regular_file(executable, ec))
        return {true, persisted.message};

    if (enabled) {
        const auto runtime = EnsureRuntime();
        if (!runtime.success) return runtime;
        if (turingdesk::wallpaper::NotifyWallpaperRuntimeEnabled(true))
            return {true, persisted.message};
        if (!LaunchRuntime(executable, L"--resume"))
            return {false, L"壁纸状态已保存，但无法通知桌面运行时。"};
        return {true, persisted.message};
    }

    if (turingdesk::wallpaper::NotifyWallpaperRuntimeEnabled(false))
        return {true, persisted.message};
    // Disable is ini-authoritative. Do not EnsureRuntime or cold-launch just to stop.
    return {true, persisted.message};
}

} // namespace turingdesk::desktop
