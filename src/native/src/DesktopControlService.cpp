#include "turingdesk/DesktopControlService.h"

#include "turingdesk/WallpaperPackage.h"

#include <windows.h>
#include <shellapi.h>

#include <filesystem>
#include <iterator>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace turingdesk::desktop {
namespace {

constexpr wchar_t kWallpaperControlClass[] = L"TuringDesk.Native.WallpaperControl";

fs::path LocalTuringDeskDirectory() {
    wchar_t local[32768]{};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", local, static_cast<DWORD>(std::size(local)));
    fs::path base = (length > 0 && length < std::size(local)) ? fs::path(local) : fs::temp_directory_path();
    fs::path directory = base / L"TuringDesk";
    std::error_code ec;
    fs::create_directories(directory, ec);
    return directory;
}

fs::path ModuleDirectory() {
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) return {};
    path.resize(length);
    return fs::path(path).parent_path();
}

std::wstring ReadProfile(const fs::path& path, const wchar_t* key, const wchar_t* fallback = L"") {
    std::vector<wchar_t> buffer(32768);
    GetPrivateProfileStringW(L"Wallpaper", key, fallback, buffer.data(),
                             static_cast<DWORD>(buffer.size()), path.c_str());
    return buffer.data();
}

DesktopControlResult StoreLoadFailure(const std::wstring& error) {
    return {false, error.empty() ? L"无法读取桌面小组件状态。" : error};
}

} // namespace

DesktopControlResult DesktopControlService::EnsureRuntime() const {
    if (FindWindowW(kWallpaperControlClass, nullptr)) return {true, L"桌面运行时已启动。"};

    const fs::path executable = ModuleDirectory() / L"TuringDeskWallpaper.exe";
    std::error_code ec;
    if (!fs::exists(executable, ec) || !fs::is_regular_file(executable, ec))
        return {false, L"找不到 TuringDeskWallpaper.exe。"};

    const HINSTANCE launched = ShellExecuteW(nullptr, L"open", executable.c_str(), nullptr,
                                             executable.parent_path().c_str(), SW_SHOWNOACTIVATE);
    if (reinterpret_cast<INT_PTR>(launched) <= 32)
        return {false, L"无法启动桌面运行时。"};
    return {true, L"桌面运行时已启动。"};
}

DesktopControlResult DesktopControlService::GetState(DesktopState* state) const {
    if (!state) return {false, L"DesktopState 输出不能为空。"};

    const fs::path config = LocalTuringDeskDirectory() / L"wallpaper.ini";
    wallpaper::DesktopWidgetStore widgets;
    std::wstring error;
    if (!widgets.Load(&error)) return StoreLoadFailure(error);

    DesktopState next;
    next.enabled = GetPrivateProfileIntW(L"Wallpaper", L"Enabled", 1, config.c_str()) != 0;
    next.scene = ReadProfile(config, L"Scene", L"aurora");
    next.layout = ReadProfile(config, L"Layout", L"span");
    next.scale = ReadProfile(config, L"Scale", L"cover");
    next.fpsCap = GetPrivateProfileIntW(L"Wallpaper", L"FpsCap", 30, config.c_str());
    next.imageOrWebSource = ReadProfile(config, L"Image", L"");
    next.videoSource = ReadProfile(config, L"Video", L"");
    next.widgetCount = widgets.Items().size();
    *state = std::move(next);
    return {true, L"桌面状态读取完成。"};
}

DesktopControlResult DesktopControlService::ApplyWebPackage(const fs::path& package) const {
    wallpaper::WallpaperPackageManifest manifest;
    std::wstring error;
    if (!wallpaper::WallpaperPackage::Validate(package, &manifest, &error))
        return {false, error.empty() ? L".tdwall 校验失败。" : error};
    if (manifest.type != wallpaper::WallpaperPackageType::Web)
        return {false, L"当前桌面控制接口只接受 Web 类型 .tdwall。"};

    std::error_code ec;
    const fs::path source = fs::absolute(package / manifest.entry, ec).lexically_normal();
    if (ec || !fs::exists(source, ec) || !fs::is_regular_file(source, ec))
        return {false, L".tdwall Web entry 不存在。"};

    const fs::path config = LocalTuringDeskDirectory() / L"wallpaper.ini";
    bool ok = true;
    ok = WritePrivateProfileStringW(L"Wallpaper", L"Enabled", L"1", config.c_str()) != FALSE && ok;
    ok = WritePrivateProfileStringW(L"Wallpaper", L"Scene", L"web", config.c_str()) != FALSE && ok;
    ok = WritePrivateProfileStringW(L"Wallpaper", L"Image", source.c_str(), config.c_str()) != FALSE && ok;
    ok = WritePrivateProfileStringW(L"Wallpaper", L"Video", L"", config.c_str()) != FALSE && ok;
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, config.c_str());
    if (!ok) return {false, L"无法保存当前 Web 桌面状态。"};

    const auto runtime = EnsureRuntime();
    if (!runtime.success) return runtime;
    return {true, L"已应用 Web 桌面：" + manifest.title + L" · " + source.wstring()};
}

DesktopControlResult DesktopControlService::CreateWebWidget(
    const WebWidgetCreateRequest& request,
    wallpaper::DesktopWidget* created) const {
    if (request.htmlUtf8.empty()) return {false, L"desktop widget html 不能为空。"};

    wallpaper::DesktopWidgetStore store;
    std::wstring error;
    if (!store.Load(&error)) return StoreLoadFailure(error);

    auto widget = store.CreateManagedWeb(
        request.title.empty() ? L"Desktop Widget" : request.title,
        request.htmlUtf8,
        request.monitorId,
        request.x,
        request.y,
        request.width,
        request.height,
        &error);
    if (!widget) return {false, error.empty() ? L"创建桌面小组件失败。" : error};

    const auto runtime = EnsureRuntime();
    if (!runtime.success) return runtime;
    if (created) *created = *widget;
    return {true, L"桌面小组件已创建：" + widget->id};
}

DesktopControlResult DesktopControlService::UpdateWidget(const WidgetUpdateRequest& request) const {
    if (request.id.empty()) return {false, L"desktop widget id 不能为空。"};
    if (request.htmlUtf8 && request.htmlUtf8->empty()) return {false, L"desktop widget html 不能为空。"};

    wallpaper::DesktopWidgetStore store;
    std::wstring error;
    if (!store.Load(&error)) return StoreLoadFailure(error);
    const auto old = store.Find(request.id);
    if (!old) return {false, L"没有找到桌面小组件：" + request.id};

    auto widget = *old;
    if (request.title) widget.title = *request.title;
    if (request.monitorId) widget.monitorId = *request.monitorId;
    if (request.x) widget.x = *request.x;
    if (request.y) widget.y = *request.y;
    if (request.width) widget.width = *request.width;
    if (request.height) widget.height = *request.height;
    if (request.zIndex) widget.zIndex = *request.zIndex;
    if (request.enabled) widget.enabled = *request.enabled;

    if (!store.Upsert(widget, &error))
        return {false, error.empty() ? L"更新桌面小组件失败。" : error};

    if (request.htmlUtf8 && !store.UpdateManagedHtml(request.id, *request.htmlUtf8, &error)) {
        std::wstring rollbackError;
        store.Upsert(*old, &rollbackError);
        return {false, error.empty() ? L"更新小组件 HTML 失败。" : error};
    }

    const auto runtime = EnsureRuntime();
    if (!runtime.success) return runtime;
    return {true, L"桌面小组件已更新：" + request.id};
}

DesktopControlResult DesktopControlService::RemoveWidget(std::wstring_view id) const {
    if (id.empty()) return {false, L"desktop widget id 不能为空。"};
    wallpaper::DesktopWidgetStore store;
    std::wstring error;
    if (!store.Load(&error)) return StoreLoadFailure(error);
    if (!store.Remove(id, true, &error))
        return {false, error.empty() ? L"删除桌面小组件失败。" : error};
    return {true, L"桌面小组件已删除：" + std::wstring(id)};
}

DesktopControlResult DesktopControlService::ListWidgets(std::vector<wallpaper::DesktopWidget>* widgets) const {
    if (!widgets) return {false, L"Widget 输出不能为空。"};
    wallpaper::DesktopWidgetStore store;
    std::wstring error;
    if (!store.Load(&error)) return StoreLoadFailure(error);
    *widgets = store.Items();
    return {true, L"桌面小组件读取完成。"};
}

} // namespace turingdesk::desktop
