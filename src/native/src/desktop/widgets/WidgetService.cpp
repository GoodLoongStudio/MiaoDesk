#include "turingdesk/WidgetService.h"

#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <iterator>
#include <vector>

namespace fs = std::filesystem;

namespace turingdesk::desktop {
namespace {

WidgetServiceResult LoadFailure(const std::wstring& error) {
    return {false, error.empty() ? L"无法读取桌面小组件状态。" : error};
}

fs::path WallpaperConfigPath() {
    wchar_t local[32768]{};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", local, static_cast<DWORD>(std::size(local)));
    fs::path dir = (length > 0 && length < std::size(local))
        ? fs::path(local) / L"TuringDesk"
        : fs::temp_directory_path() / L"TuringDesk";
    return dir / L"wallpaper.ini";
}

std::wstring ReadWidgetRuntimeDetail() {
    std::vector<wchar_t> buffer(32768);
    GetPrivateProfileStringW(
        L"Diagnostics", L"WidgetRuntime", L"",
        buffer.data(), static_cast<DWORD>(buffer.size()), WallpaperConfigPath().c_str());
    return buffer.data();
}

bool RuntimeDetailLooksHealthy(const std::wstring& detail) {
    if (detail.empty()) return false;
    if (detail.find(L"运行异常") != std::wstring::npos ||
        detail.find(L"启动失败") != std::wstring::npos ||
        detail.find(L"找不到") != std::wstring::npos ||
        detail.find(L"unavailable") != std::wstring::npos ||
        detail.find(L"failed") != std::wstring::npos) return false;
    return detail.find(L"WebView2 隔离 Surface") != std::wstring::npos;
}

} // namespace

WidgetServiceResult WidgetService::CreateWeb(
    const WebWidgetCreateRequest& request,
    wallpaper::DesktopWidget* created) const {
    if (request.htmlUtf8.empty()) return {false, L"desktop widget html 不能为空。"};

    wallpaper::DesktopWidgetStore store;
    std::wstring error;
    if (!store.Load(&error)) return LoadFailure(error);
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
    if (created) *created = *widget;
    return {true, L"桌面小组件已创建：" + widget->id};
}

WidgetServiceResult WidgetService::Update(const WidgetUpdateRequest& request) const {
    if (request.id.empty()) return {false, L"desktop widget id 不能为空。"};
    if (request.htmlUtf8 && request.htmlUtf8->empty()) return {false, L"desktop widget html 不能为空。"};

    wallpaper::DesktopWidgetStore store;
    std::wstring error;
    if (!store.Load(&error)) return LoadFailure(error);
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
    return {true, L"桌面小组件已更新：" + request.id};
}

WidgetServiceResult WidgetService::Remove(std::wstring_view id) const {
    if (id.empty()) return {false, L"desktop widget id 不能为空。"};
    wallpaper::DesktopWidgetStore store;
    std::wstring error;
    if (!store.Load(&error)) return LoadFailure(error);
    if (!store.Remove(id, true, &error))
        return {false, error.empty() ? L"删除桌面小组件失败。" : error};
    return {true, L"桌面小组件已删除：" + std::wstring(id)};
}

WidgetServiceResult WidgetService::List(std::vector<wallpaper::DesktopWidget>* widgets) const {
    if (!widgets) return {false, L"Widget 输出不能为空。"};
    wallpaper::DesktopWidgetStore store;
    std::wstring error;
    if (!store.Load(&error)) return LoadFailure(error);
    *widgets = store.Items();
    return {true, L"桌面小组件读取完成。"};
}

WidgetServiceResult WidgetService::Find(std::wstring_view id, wallpaper::DesktopWidget* widget) const {
    if (!widget) return {false, L"Widget 输出不能为空。"};
    wallpaper::DesktopWidgetStore store;
    std::wstring error;
    if (!store.Load(&error)) return LoadFailure(error);
    const auto found = store.Find(id);
    if (!found) return {false, L"没有找到桌面小组件：" + std::wstring(id)};
    *widget = *found;
    return {true, L"桌面小组件读取完成。"};
}

WidgetServiceResult WidgetService::GetRuntimeHealth(WidgetRuntimeHealth* health) const {
    if (!health) return {false, L"WidgetRuntimeHealth 输出不能为空。"};

    wallpaper::DesktopWidgetStore store;
    std::wstring error;
    if (!store.Load(&error)) return LoadFailure(error);

    WidgetRuntimeHealth result;
    result.configuredCount = store.Items().size();
    result.enabledWebCount = static_cast<std::size_t>(std::count_if(
        store.Items().begin(), store.Items().end(), [](const wallpaper::DesktopWidget& widget) {
            return widget.enabled && widget.kind == wallpaper::DesktopWidgetKind::Web;
        }));
    result.detail = ReadWidgetRuntimeDetail();
    result.runtimeReported = !result.detail.empty();
    result.runtimeHealthy = result.enabledWebCount == 0
        ? (result.detail.empty() || result.detail.find(L"未启用桌面小组件") != std::wstring::npos ||
           result.detail.find(L"Widget runtime stopped") != std::wstring::npos)
        : RuntimeDetailLooksHealthy(result.detail);
    *health = std::move(result);
    return {true, L"桌面小组件运行状态读取完成。"};
}

} // namespace turingdesk::desktop