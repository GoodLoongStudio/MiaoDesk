#include "turingdesk/DesktopWidgetController.h"
#include "turingdesk/RuntimeLogPaths.h"

#include <windows.h>

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <utility>

namespace turingdesk::desktop {
namespace {

const wchar_t* BoolText(bool value) noexcept { return value ? L"true" : L"false"; }

struct PresetGeometry {
    float width;
    float height;
};

PresetGeometry GeometryFor(WidgetSizePreset size) noexcept {
    switch (size) {
    case WidgetSizePreset::Small: return {0.18f, 0.11f};
    case WidgetSizePreset::Large: return {0.32f, 0.22f};
    case WidgetSizePreset::Medium: default: return {0.23f, 0.16f};
    }
}

bool SameMonitor(const wallpaper::DesktopWidget& widget, std::wstring_view monitorId) {
    if (widget.monitorId.empty() && monitorId.empty()) return true;
    return _wcsicmp(widget.monitorId.c_str(), std::wstring(monitorId).c_str()) == 0;
}

std::pair<float, float> AutomaticPlacement(
    const std::vector<wallpaper::DesktopWidget>& widgets,
    std::wstring_view monitorId,
    PresetGeometry geometry) {
    // Product rule: start at the top-right with a 3% logical margin, then stack
    // downward. When a column is full, continue one column to the left. Users do
    // not need to understand normalized desktop coordinates.
    constexpr float margin = 0.03f;
    constexpr float gap = 0.025f;
    std::size_t occupied = 0;
    for (const auto& widget : widgets) {
        if (widget.enabled && SameMonitor(widget, monitorId)) ++occupied;
    }

    const float rowStep = geometry.height + gap;
    const int rows = std::max(1, static_cast<int>((1.0f - 2.0f * margin + gap) / rowStep));
    const int row = static_cast<int>(occupied % static_cast<std::size_t>(rows));
    const int column = static_cast<int>(occupied / static_cast<std::size_t>(rows));
    const float x = std::max(margin, 1.0f - margin - geometry.width - column * (geometry.width + gap));
    const float y = std::min(1.0f - margin - geometry.height, margin + row * rowStep);
    return {x, y};
}

void AppendControllerErrorLog(std::wstring_view message) {
    const auto path = turingdesk::RuntimeLogPath(L"widget-runtime.log");
    if (path.empty()) return;
    std::wofstream log(path, std::ios::app);
    if (!log) return;
    SYSTEMTIME now{};
    GetLocalTime(&now);
    log << L"\n=== "
        << std::setfill(L'0') << std::setw(4) << now.wYear << L'-'
        << std::setw(2) << now.wMonth << L'-' << std::setw(2) << now.wDay << L' '
        << std::setw(2) << now.wHour << L':' << std::setw(2) << now.wMinute << L':'
        << std::setw(2) << now.wSecond << L" Widget controller error ===\n"
        << message << L"\n";
}

void AppendWidgetRuntimeLog(const DesktopSnapshot& snapshot) {
    const auto path = turingdesk::RuntimeLogPath(L"widget-runtime.log");
    if (path.empty()) return;
    std::wofstream log(path, std::ios::app);
    if (!log) return;

    SYSTEMTIME now{};
    GetLocalTime(&now);
    log << L"\n=== "
        << std::setfill(L'0') << std::setw(4) << now.wYear << L'-'
        << std::setw(2) << now.wMonth << L'-' << std::setw(2) << now.wDay << L' '
        << std::setw(2) << now.wHour << L':' << std::setw(2) << now.wMinute << L':'
        << std::setw(2) << now.wSecond << L" Widget snapshot ===\n";

    log << L"desktop.enabled=" << BoolText(snapshot.desktop.enabled)
        << L" scene=" << snapshot.desktop.scene
        << L" layout=" << snapshot.desktop.layout
        << L" configuredWidgets=" << snapshot.widgets.size()
        << L" enabledWeb=" << snapshot.widgetRuntime.enabledWebCount
        << L" runtimeReported=" << BoolText(snapshot.widgetRuntime.runtimeReported)
        << L" runtimeHealthy=" << BoolText(snapshot.widgetRuntime.runtimeHealthy)
        << L" detail=" << snapshot.widgetRuntime.detail << L"\n";

    for (const auto& widget : snapshot.widgets) {
        log << L"config id=" << widget.id << L" title=\"" << widget.title << L"\""
            << L" enabled=" << BoolText(widget.enabled)
            << L" kind=" << (widget.kind == wallpaper::DesktopWidgetKind::Web ? L"web" : L"unknown")
            << L" monitor=\"" << (widget.monitorId.empty() ? L"<primary>" : widget.monitorId) << L"\""
            << L" x=" << widget.x << L" y=" << widget.y
            << L" width=" << widget.width << L" height=" << widget.height
            << L" zIndex=" << widget.zIndex
            << L" source=\"" << widget.source.wstring() << L"\"\n";
    }

    for (const auto& surface : snapshot.widgetRuntime.surfaces) {
        log << L"surface id=" << surface.widgetId
            << L" monitor=\"" << surface.monitorId << L"\""
            << L" configured=" << BoolText(surface.configured)
            << L" pid=" << surface.processId
            << L" processRunning=" << BoolText(surface.processRunning)
            << L" hwnd=0x" << std::hex << surface.hwndValue << std::dec
            << L" hwndReady=" << BoolText(surface.hwndReady)
            << L" parentValid=" << BoolText(surface.parentValid)
            << L" childStyleValid=" << BoolText(surface.childStyleValid)
            << L" visible=" << BoolText(surface.visible)
            << L" monitorReported=" << BoolText(surface.monitorReported)
            << L" monitorValid=" << BoolText(surface.monitorValid)
            << L" geometryReported=" << BoolText(surface.geometryReported)
            << L" geometryValid=" << BoolText(surface.geometryValid)
            << L" expected=[" << surface.expectedLeft << L',' << surface.expectedTop << L','
            << surface.expectedRight << L',' << surface.expectedBottom << L']'
            << L" actual=[" << surface.actualLeft << L',' << surface.actualTop << L','
            << surface.actualRight << L',' << surface.actualBottom << L']'
            << L" environment=" << BoolText(surface.environmentReported) << L'/' << BoolText(surface.environmentReady)
            << L" controller=" << BoolText(surface.controllerReported) << L'/' << BoolText(surface.controllerReady)
            << L" navigation=" << BoolText(surface.navigationReported) << L'/' << BoolText(surface.navigationReady)
            << L" zOrder=" << BoolText(surface.zOrderReported) << L'/' << BoolText(surface.zOrderValid)
            << L" renderingHealthy=" << BoolText(surface.renderingHealthy)
            << L" issue=\"" << surface.issueCode << L"\""
            << L" detail=\"" << surface.detail << L"\""
            << L" action=\"" << surface.recommendedAction << L"\"\n";
    }
    log.flush();
}

} // namespace

DesktopControlResult DesktopWidgetController::Refresh(std::vector<wallpaper::DesktopWidget>* widgets) const {
    return service_.ListWidgets(widgets);
}

DesktopControlResult DesktopWidgetController::Find(std::wstring_view id, wallpaper::DesktopWidget* widget) const {
    if (!widget) return {false, L"Widget 输出不能为空。"};
    if (id.empty()) return {false, L"desktop widget id 不能为空。"};
    std::vector<wallpaper::DesktopWidget> widgets;
    const auto result = service_.ListWidgets(&widgets);
    if (!result.success) return result;
    const auto found = std::find_if(widgets.begin(), widgets.end(), [&](const auto& candidate) { return candidate.id == id; });
    if (found == widgets.end()) return {false, L"没有找到桌面小组件：" + std::wstring(id)};
    *widget = *found;
    return {true, L"桌面小组件读取完成。"};
}

DesktopControlResult DesktopWidgetController::RuntimeHealth(WidgetRuntimeHealth* health) const {
    if (!health) return {false, L"WidgetRuntimeHealth 输出不能为空。"};
    const auto runtime = service_.EnsureRuntime();
    if (!runtime.success) {
        AppendControllerErrorLog(L"EnsureRuntime failed: " + runtime.message);
        return runtime;
    }
    DesktopSnapshot snapshot;
    const auto result = service_.GetSnapshot(&snapshot);
    if (!result.success) {
        AppendControllerErrorLog(L"GetSnapshot failed: " + result.message);
        return result;
    }
    AppendWidgetRuntimeLog(snapshot);
    *health = std::move(snapshot.widgetRuntime);
    return {true, L"桌面小组件运行状态读取完成。"};
}

DesktopControlResult DesktopWidgetController::CreateClock(
    std::wstring monitorId,
    wallpaper::DesktopWidget* created) const {
    return CreateClock(std::move(monitorId), WidgetSizePreset::Medium, created);
}

DesktopControlResult DesktopWidgetController::CreateClock(
    std::wstring monitorId,
    WidgetSizePreset size,
    wallpaper::DesktopWidget* created) const {
    static constexpr std::string_view html = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><style>
html,body{margin:0;width:100%;height:100%;overflow:hidden;background:transparent;font-family:"Segoe UI",sans-serif;color:white}
.card{box-sizing:border-box;width:100%;height:100%;display:flex;flex-direction:column;justify-content:center;padding:18px 22px;border-radius:22px;background:rgba(18,24,38,.78);box-shadow:0 10px 30px rgba(0,0,0,.28)}
#time{font-size:clamp(30px,14vw,52px);font-weight:650;letter-spacing:-1px;line-height:1}#date{margin-top:10px;font-size:clamp(12px,5vw,17px);opacity:.78}
</style></head><body><div class="card"><div id="time"></div><div id="date"></div></div><script>
function tick(){const d=new Date();document.getElementById('time').textContent=d.toLocaleTimeString([], {hour:'2-digit',minute:'2-digit'});document.getElementById('date').textContent=d.toLocaleDateString([], {weekday:'long',year:'numeric',month:'long',day:'numeric'});}tick();setInterval(tick,1000);
</script></body></html>)HTML";

    std::vector<wallpaper::DesktopWidget> existing;
    const auto listed = service_.ListWidgets(&existing);
    if (!listed.success) return listed;

    const auto geometry = GeometryFor(size);
    const auto [x, y] = AutomaticPlacement(existing, monitorId, geometry);
    WebWidgetCreateRequest request;
    request.title = L"桌面时钟";
    request.htmlUtf8.assign(html.begin(), html.end());
    request.monitorId = std::move(monitorId);
    request.x = x;
    request.y = y;
    request.width = geometry.width;
    request.height = geometry.height;
    return service_.CreateWebWidget(request, created);
}

DesktopControlResult DesktopWidgetController::SetSize(std::wstring_view id, WidgetSizePreset size) const {
    const auto geometry = GeometryFor(size);
    WidgetUpdateRequest request;
    request.id = std::wstring(id);
    request.width = geometry.width;
    request.height = geometry.height;
    return service_.UpdateWidget(request);
}

DesktopControlResult DesktopWidgetController::MoveToMonitor(std::wstring_view id, std::wstring monitorId) const {
    std::vector<wallpaper::DesktopWidget> existing;
    const auto listed = service_.ListWidgets(&existing);
    if (!listed.success) return listed;
    wallpaper::DesktopWidget current;
    const auto found = Find(id, &current);
    if (!found.success) return found;
    const PresetGeometry geometry{current.width, current.height};
    const auto [x, y] = AutomaticPlacement(existing, monitorId, geometry);
    WidgetUpdateRequest request;
    request.id = std::wstring(id);
    request.monitorId = std::move(monitorId);
    request.x = x;
    request.y = y;
    return service_.UpdateWidget(request);
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