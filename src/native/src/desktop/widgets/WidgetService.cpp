#include "turingdesk/WidgetService.h"

namespace turingdesk::desktop {
namespace {

WidgetServiceResult LoadFailure(const std::wstring& error) {
    return {false, error.empty() ? L"无法读取桌面小组件状态。" : error};
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

} // namespace turingdesk::desktop
