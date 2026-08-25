#include "turingdesk/DesktopWidgetController.h"

#include <algorithm>
#include <utility>

namespace turingdesk::desktop {

DesktopControlResult DesktopWidgetController::Refresh(std::vector<wallpaper::DesktopWidget>* widgets) const {
    return service_.ListWidgets(widgets);
}

DesktopControlResult DesktopWidgetController::Find(std::wstring_view id, wallpaper::DesktopWidget* widget) const {
    if (!widget) return {false, L"Widget 输出不能为空。"};
    if (id.empty()) return {false, L"desktop widget id 不能为空。"};

    std::vector<wallpaper::DesktopWidget> widgets;
    const auto result = service_.ListWidgets(&widgets);
    if (!result.success) return result;

    const auto found = std::find_if(widgets.begin(), widgets.end(), [&](const auto& candidate) {
        return candidate.id == id;
    });
    if (found == widgets.end()) return {false, L"没有找到桌面小组件：" + std::wstring(id)};
    *widget = *found;
    return {true, L"桌面小组件读取完成。"};
}

DesktopControlResult DesktopWidgetController::RuntimeHealth(WidgetRuntimeHealth* health) const {
    if (!health) return {false, L"WidgetRuntimeHealth 输出不能为空。"};
    DesktopSnapshot snapshot;
    const auto result = service_.GetSnapshot(&snapshot);
    if (!result.success) return result;
    *health = std::move(snapshot.widgetRuntime);
    return {true, L"桌面小组件运行状态读取完成。"};
}

DesktopControlResult DesktopWidgetController::CreateClock(
    std::wstring monitorId,
    wallpaper::DesktopWidget* created) const {
    static constexpr std::string_view html = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><style>
html,body{margin:0;width:100%;height:100%;overflow:hidden;background:transparent;font-family:"Segoe UI",sans-serif;color:white}
.card{box-sizing:border-box;width:100%;height:100%;display:flex;flex-direction:column;justify-content:center;padding:18px 22px;border-radius:22px;background:rgba(18,24,38,.78);box-shadow:0 10px 30px rgba(0,0,0,.28)}
#time{font-size:44px;font-weight:650;letter-spacing:-1px;line-height:1}#date{margin-top:10px;font-size:16px;opacity:.78}
</style></head><body><div class="card"><div id="time"></div><div id="date"></div></div><script>
function tick(){const d=new Date();document.getElementById('time').textContent=d.toLocaleTimeString([], {hour:'2-digit',minute:'2-digit'});document.getElementById('date').textContent=d.toLocaleDateString([], {weekday:'long',year:'numeric',month:'long',day:'numeric'});}tick();setInterval(tick,1000);
</script></body></html>)HTML";

    WebWidgetCreateRequest request;
    request.title = L"桌面时钟";
    request.htmlUtf8.assign(html.begin(), html.end());
    request.monitorId = std::move(monitorId);
    request.x = 0.72f;
    request.y = 0.05f;
    request.width = 0.23f;
    request.height = 0.16f;
    return service_.CreateWebWidget(request, created);
}

DesktopControlResult DesktopWidgetController::SetEnabled(std::wstring_view id, bool enabled) const {
    WidgetUpdateRequest request;
    request.id = std::wstring(id);
    request.enabled = enabled;
    return service_.UpdateWidget(request);
}

DesktopControlResult DesktopWidgetController::Remove(std::wstring_view id) const {
    return service_.RemoveWidget(id);
}

} // namespace turingdesk::desktop