#include "turingdesk/DesktopWidgetUiAdapter.h"

#include <algorithm>
#include <utility>

namespace turingdesk::wallpaper {

bool DesktopWidgetUiAdapter::AssignError(const desktop::DesktopControlResult& result, std::wstring* error) {
    if (result.success) return true;
    if (error) *error = result.message;
    return false;
}

bool DesktopWidgetUiAdapter::Refresh(std::wstring* error) {
    std::vector<DesktopWidget> next;
    const auto result = service_.ListWidgets(&next);
    if (!AssignError(result, error)) return false;
    items_ = std::move(next);
    return true;
}

bool DesktopWidgetUiAdapter::Load(std::wstring* error) {
    return Refresh(error);
}

const std::vector<DesktopWidget>& DesktopWidgetUiAdapter::Items() const noexcept {
    return items_;
}

std::optional<DesktopWidget> DesktopWidgetUiAdapter::Find(std::wstring_view id) const {
    const auto found = std::find_if(items_.begin(), items_.end(), [&](const auto& widget) {
        return widget.id == id;
    });
    if (found == items_.end()) return std::nullopt;
    return *found;
}

bool DesktopWidgetUiAdapter::RuntimeHealth(desktop::WidgetRuntimeHealth* health, std::wstring* error) const {
    if (!health) {
        if (error) *error = L"WidgetRuntimeHealth 输出不能为空。";
        return false;
    }
    desktop::DesktopSnapshot snapshot;
    const auto result = service_.GetSnapshot(&snapshot);
    if (!AssignError(result, error)) return false;
    *health = std::move(snapshot.widgetRuntime);
    return true;
}

std::optional<DesktopWidget> DesktopWidgetUiAdapter::CreateManagedWeb(
    std::wstring title,
    std::string_view htmlUtf8,
    std::wstring monitorId,
    float x,
    float y,
    float width,
    float height,
    std::wstring* error) {
    desktop::WebWidgetCreateRequest request;
    request.title = std::move(title);
    request.htmlUtf8.assign(htmlUtf8.begin(), htmlUtf8.end());
    request.monitorId = std::move(monitorId);
    request.x = x;
    request.y = y;
    request.width = width;
    request.height = height;

    DesktopWidget created;
    const auto result = service_.CreateWebWidget(request, &created);
    if (!AssignError(result, error)) return std::nullopt;
    Refresh(nullptr);
    return created;
}

std::optional<DesktopWidget> DesktopWidgetUiAdapter::Upsert(DesktopWidget widget, std::wstring* error) {
    if (widget.id.empty()) {
        if (error) *error = L"Legacy widget UI adapter cannot upsert a widget without id.";
        return std::nullopt;
    }

    desktop::WidgetUpdateRequest request;
    request.id = widget.id;
    request.title = widget.title;
    request.monitorId = widget.monitorId;
    request.x = widget.x;
    request.y = widget.y;
    request.width = widget.width;
    request.height = widget.height;
    request.zIndex = widget.zIndex;
    request.enabled = widget.enabled;

    const auto result = service_.UpdateWidget(request);
    if (!AssignError(result, error)) return std::nullopt;
    Refresh(nullptr);
    return widget;
}

bool DesktopWidgetUiAdapter::Remove(std::wstring_view id, bool, std::wstring* error) {
    const auto result = service_.RemoveWidget(id);
    if (!AssignError(result, error)) return false;
    Refresh(nullptr);
    return true;
}

} // namespace turingdesk::wallpaper