#include "miaodesk/DesktopControlService.h"

#include "miaodesk/WallpaperRuntimeControl.h"
#include "miaodesk/RuntimeLogger.h"

#include <windows.h>
#include <shellapi.h>

#include <filesystem>
#include <iterator>
#include <system_error>

namespace fs = std::filesystem;

namespace miaodesk::desktop {
namespace {

constexpr wchar_t kWallpaperControlClass[] = L"MiaoDesk.Native.WallpaperControl";
constexpr wchar_t kShellMutex[] = L"Local\\MiaoDesk.DesktopShellSupervisor.v1";
constexpr wchar_t kWebRuntimeMutex[] = L"Local\\MiaoDesk.WebWallpaperRuntime.v1";
constexpr wchar_t kWidgetRuntimeMutex[] = L"Local\\MiaoDesk.WidgetRuntime.v1";
constexpr wchar_t kShellMode[] = L"--desktop-shell-supervisor";
constexpr wchar_t kWebRuntimeMode[] = L"--web-wallpaper-runtime";
constexpr wchar_t kWidgetRuntimeMode[] = L"--widget-runtime";
constexpr wchar_t kNativeWidgetHostMessageClass[] = L"MiaoDesk.Native.WidgetHostMessage";
constexpr wchar_t kWidgetRuntimeReloadMessageName[] = L"MiaoDesk.WidgetRuntimeReload.v1";
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

void NotifyNativeWidgetRuntimeReload() {
    const HWND messageWindow = FindWindowExW(
        HWND_MESSAGE, nullptr, kNativeWidgetHostMessageClass, nullptr);
    if (!messageWindow || !IsWindow(messageWindow)) return;
    const UINT message = RegisterWindowMessageW(kWidgetRuntimeReloadMessageName);
    if (message != 0) PostMessageW(messageWindow, message, 0, 0);
}

DesktopControlResult RefreshWidgetRuntime(const DesktopControlResult& result,
                                          const DesktopControlService& service) {
    const auto runtime = service.EnsureRuntime();
    if (!runtime.success) return runtime;
    // The coordinator still polls as a recovery path. This message makes an
    // already-running native host apply create/update/remove immediately.
    NotifyNativeWidgetRuntimeReload();
    return result;
}

DesktopControlResult FromWallpaper(WallpaperServiceResult result) {
    return {result.success, result.message};
}

DesktopControlResult FromWidget(WidgetServiceResult result) {
    return {result.success, result.message};
}

const wchar_t* ContentKindText(content::ContentKind kind) noexcept {
    return kind == content::ContentKind::Widget ? L"小组件" : L"壁纸";
}

} // namespace

DesktopControlResult DesktopControlService::EnsureRuntime() const {
    if (RuntimeInfrastructureReady())
        return {true, L"桌面运行时与隔离 Shell/Web/Widget helper 已就绪。"};

    const fs::path executable = ModuleDirectory() / L"MiaoDeskWallpaper.exe";
    std::error_code ec;
    if (!fs::exists(executable, ec) || !fs::is_regular_file(executable, ec))
        return {false, L"找不到 MiaoDeskWallpaper.exe。"};

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
        return {false, L"桌面运行时进程已启动，但 Native/Shell/Web/Widget 故障域在 5 秒内没有全部就绪。请查看 MiaoDesk-Logs。"};
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
    widgetService.List(&widgets);

    WidgetRuntimeHealth widgetRuntime;
    widgetService.GetRuntimeHealth(&widgetRuntime);

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
    WallpaperService wallpaperService;
    WallpaperState wallpaperState;
    const auto wallpaperResult = wallpaperService.GetState(&wallpaperState);
    if (!wallpaperResult.success) return FromWallpaper(wallpaperResult);

    WidgetService widgetService;
    std::vector<wallpaper::DesktopWidget> widgets;
    widgetService.List(&widgets);

    state->enabled = wallpaperState.enabled;
    state->scene = wallpaperState.scene;
    state->layout = wallpaperState.layout;
    state->scale = wallpaperState.scale;
    state->fpsCap = wallpaperState.fpsCap;
    state->imageOrWebSource = wallpaperState.imageOrWebSource;
    state->videoSource = wallpaperState.videoSource;
    state->widgetCount = widgets.size();
    return {true, L"桌面状态读取完成。"};
}

DesktopControlResult DesktopControlService::InspectContentPackage(
    const fs::path& package,
    content::ManagedContentPackageInfo* info) const {
    if (!info) return {false, L"Content package info 输出不能为空。"};
    std::wstring error;
    if (!content::MiaoContentPackageManager::Inspect(package, info, &error)) {
        miaodesk::log::Error(L"DesktopControl", L"InspectContentPackage 失败: " + error);
        return {false, error.empty() ? L"内容包校验失败。" : error};
    }
    return {true, L"内容包已识别：" + info->source};
}

DesktopControlResult DesktopControlService::InstallContentPackage(
    const fs::path& package,
    content::ContentPackageInstallResult* installed,
    const content::ContentPackageInstallOptions& options) const {
    if (!installed) return {false, L"Content package install result 输出不能为空。"};
    std::wstring error;
    if (!content::MiaoContentPackageManager::Install(package, installed, options, &error)) {
        miaodesk::log::Error(L"DesktopControl", L"InstallContentPackage 失败: " + error);
        return {false, error.empty() ? L"内容包安装失败。" : error};
    }

    const auto& info = installed->package;
    const std::wstring action = installed->replacedExisting ? L"已替换" : L"已安装";
    miaodesk::log::Info(
        L"DesktopControl",
        action + std::wstring(L" Content ") + ContentKindText(info.kind) + L": " + info.source +
            L" -> " + info.packageRoot.wstring());

    // A package install does not create a widget instance or select a wallpaper.
    // Runtime/catalog consumers resolve the stable content:<id> source when they
    // next refresh; callers can explicitly create/apply content afterwards.
    return {true, action + std::wstring(L" ") + ContentKindText(info.kind) + L"：" + info.name +
                  L"（" + info.source + L"）"};
}

DesktopControlResult DesktopControlService::ResolveContentPackage(
    content::ContentKind kind,
    std::wstring_view source,
    content::ManagedContentPackageInfo* info) const {
    if (!info) return {false, L"Content package info 输出不能为空。"};
    std::wstring error;
    if (!content::MiaoContentPackageManager::Resolve(kind, source, info, &error))
        return {false, error.empty() ? L"无法解析内容包。" : error};
    return {true, L"内容包已解析：" + info->source};
}

DesktopControlResult DesktopControlService::ApplyWebPackage(const fs::path& package) const {
    miaodesk::log::Info(L"DesktopControl", L"ApplyWebPackage: " + package.wstring());
    WallpaperService service;
    const auto result = service.ApplyWebPackage(package);
    if (!result.success) {
        miaodesk::log::Error(L"DesktopControl", L"ApplyWebPackage 失败: " + result.message);
        return FromWallpaper(result);
    }
    const auto runtime = EnsureRuntime();
    if (!runtime.success) {
        miaodesk::log::Error(L"DesktopControl", L"EnsureRuntime 失败: " + runtime.message);
        return runtime;
    }
    miaodesk::wallpaper::NotifyWallpaperRuntimeReload();
    miaodesk::log::Info(L"DesktopControl", L"ApplyWebPackage 成功完成并已通知重载");
    return {true, result.message};
}

DesktopControlResult DesktopControlService::ApplyLibraryItem(const wallpaper::WallpaperLibraryItem& item) const {
    miaodesk::log::Info(L"DesktopControl", L"ApplyLibraryItem: id=" + item.id + L", title=\"" + item.title + L"\"");
    WallpaperService service;
    const auto result = service.ApplyLibraryItem(item);
    if (!result.success) {
        miaodesk::log::Error(L"DesktopControl", L"ApplyLibraryItem 失败: " + result.message);
        return FromWallpaper(result);
    }
    const auto runtime = EnsureRuntime();
    if (!runtime.success) {
        miaodesk::log::Error(L"DesktopControl", L"EnsureRuntime 失败: " + runtime.message);
        return runtime;
    }
    miaodesk::wallpaper::NotifyWallpaperRuntimeReload();
    miaodesk::log::Info(L"DesktopControl", L"ApplyLibraryItem 成功完成并已通知重载");
    return {true, result.message};
}

DesktopControlResult DesktopControlService::AssignLibraryItemToMonitor(
    const wallpaper::WallpaperLibraryItem& item,
    std::wstring_view monitorId,
    std::wstring_view friendlyName) const {
    miaodesk::log::Info(L"DesktopControl", L"AssignLibraryItemToMonitor: id=" + item.id + L", monitor=" + std::wstring(monitorId));
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

DesktopControlResult DesktopControlService::CreateNativeWidget(
    const NativeWidgetCreateRequest& request,
    wallpaper::DesktopWidget* created) const {
    WidgetService service;
    const auto result = service.CreateNative(request, created);
    if (!result.success) return FromWidget(result);
    return RefreshWidgetRuntime({true, result.message}, *this);
}

DesktopControlResult DesktopControlService::UpdateWidget(const WidgetUpdateRequest& request) const {
    WidgetService service;
    const auto result = service.Update(request);
    if (!result.success) return FromWidget(result);
    return RefreshWidgetRuntime({true, result.message}, *this);
}

DesktopControlResult DesktopControlService::RemoveWidget(std::wstring_view id) const {
    WidgetService service;
    const auto result = FromWidget(service.Remove(id));
    if (!result.success) return result;
    return RefreshWidgetRuntime(result, *this);
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
    miaodesk::log::Info(L"DesktopControl", L"SetWallpaperEnabled(" + std::wstring(enabled ? L"true" : L"false") + L") 请求");
    WallpaperService service;
    const auto persisted = service.SetEnabled(enabled);
    if (!persisted.success) {
        miaodesk::log::Error(L"DesktopControl", L"保存壁纸状态失败: " + persisted.message);
        return FromWallpaper(persisted);
    }

    const fs::path executable = ModuleDirectory() / L"MiaoDeskWallpaper.exe";
    std::error_code ec;
    if (!fs::exists(executable, ec) || !fs::is_regular_file(executable, ec)) {
        miaodesk::log::Info(L"DesktopControl", L"找不到 MiaoDeskWallpaper.exe，状态仅保存至 ini");
        return {true, persisted.message};
    }

    if (enabled) {
        const auto runtime = EnsureRuntime();
        if (!runtime.success) {
            miaodesk::log::Error(L"DesktopControl", L"EnsureRuntime 失败: " + runtime.message);
            return runtime;
        }
        if (miaodesk::wallpaper::NotifyWallpaperRuntimeEnabled(true)) {
            miaodesk::log::Info(L"DesktopControl", L"成功向壁纸控制窗口发送启用消息");
            return {true, persisted.message};
        }
        if (!LaunchRuntime(executable, L"--resume")) {
            miaodesk::log::Error(L"DesktopControl", L"启动运行进程 --resume 失败");
            return {false, L"壁纸状态已保存，但无法通知桌面运行时。"};
        }
        miaodesk::log::Info(L"DesktopControl", L"成功通过 --resume 启动壁纸运行时");
        return {true, persisted.message};
    }

    if (miaodesk::wallpaper::NotifyWallpaperRuntimeEnabled(false)) {
        miaodesk::log::Info(L"DesktopControl", L"成功向壁纸控制窗口发送停用消息");
        return {true, persisted.message};
    }
    // Disable is ini-authoritative. Do not EnsureRuntime or cold-launch just to stop.
    miaodesk::log::Info(L"DesktopControl", L"壁纸停用状态已权威保存至 ini");
    return {true, persisted.message};
}

} // namespace miaodesk::desktop
