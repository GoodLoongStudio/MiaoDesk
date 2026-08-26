#include "turingdesk/DesktopWidgetController.h"
#include "turingdesk/RuntimeLogPaths.h"

#include <windows.h>

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <string_view>
#include <utility>

namespace turingdesk::desktop {
namespace {

const wchar_t* BoolText(bool value) noexcept { return value ? L"true" : L"false"; }

struct FixedPresetSpec {
    const wchar_t* title;
    float width;
    float height;
    std::string_view html;
};

constexpr float kPlacementMargin = 0.03f;
constexpr float kPlacementGap = 0.025f;

constexpr std::string_view kMinimalClockHtml = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><style>
html,body{margin:0;width:100%;height:100%;overflow:hidden;background:transparent;font-family:"Segoe UI Variable Text","Segoe UI",sans-serif;color:white}
.card{box-sizing:border-box;width:100%;height:100%;display:flex;align-items:center;justify-content:center;border-radius:22px;background:rgba(14,18,26,.78);box-shadow:0 10px 28px rgba(0,0,0,.30);backdrop-filter:blur(18px)}
#time{font-size:clamp(34px,20vw,58px);font-weight:650;letter-spacing:-1.5px;line-height:1}
</style></head><body><div class="card"><div id="time"></div></div><script>
function tick(){const d=new Date();document.getElementById('time').textContent=d.toLocaleTimeString([], {hour:'2-digit',minute:'2-digit'});}tick();setInterval(tick,1000);
</script></body></html>)HTML";

constexpr std::string_view kDateClockHtml = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><style>
html,body{margin:0;width:100%;height:100%;overflow:hidden;background:transparent;font-family:"Segoe UI Variable Text","Segoe UI",sans-serif;color:white}
.card{box-sizing:border-box;width:100%;height:100%;display:flex;flex-direction:column;justify-content:center;padding:18px 22px;border-radius:24px;background:linear-gradient(145deg,rgba(24,31,48,.92),rgba(13,17,27,.82));box-shadow:0 12px 34px rgba(0,0,0,.32)}
#time{font-size:clamp(32px,14vw,54px);font-weight:680;letter-spacing:-1.2px;line-height:1}#date{margin-top:11px;font-size:clamp(12px,5vw,17px);opacity:.78;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
</style></head><body><div class="card"><div id="time"></div><div id="date"></div></div><script>
function tick(){const d=new Date();document.getElementById('time').textContent=d.toLocaleTimeString([], {hour:'2-digit',minute:'2-digit'});document.getElementById('date').textContent=d.toLocaleDateString([], {weekday:'long',year:'numeric',month:'long',day:'numeric'});}tick();setInterval(tick,1000);
</script></body></html>)HTML";

constexpr std::string_view kGlassClockHtml = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><style>
html,body{margin:0;width:100%;height:100%;overflow:hidden;background:transparent;font-family:"Segoe UI Variable Text","Segoe UI",sans-serif;color:white}
.card{box-sizing:border-box;width:100%;height:100%;position:relative;display:flex;flex-direction:column;justify-content:flex-end;padding:24px 26px;border:1px solid rgba(255,255,255,.20);border-radius:30px;background:linear-gradient(135deg,rgba(255,255,255,.18),rgba(255,255,255,.06));box-shadow:0 18px 42px rgba(0,0,0,.28);backdrop-filter:blur(26px)}
.glow{position:absolute;width:46%;aspect-ratio:1;border-radius:999px;right:-8%;top:-24%;background:rgba(94,140,255,.32);filter:blur(24px)}
#time{position:relative;font-size:clamp(42px,14vw,72px);font-weight:620;letter-spacing:-2px;line-height:1}#date{position:relative;margin-top:12px;font-size:clamp(13px,4vw,18px);opacity:.82}
</style></head><body><div class="card"><div class="glow"></div><div id="time"></div><div id="date"></div></div><script>
function tick(){const d=new Date();document.getElementById('time').textContent=d.toLocaleTimeString([], {hour:'2-digit',minute:'2-digit'});document.getElementById('date').textContent=d.toLocaleDateString([], {weekday:'long',month:'long',day:'numeric'});}tick();setInterval(tick,1000);
</script></body></html>)HTML";

FixedPresetSpec PresetSpec(WidgetFixedPreset preset) noexcept {
    switch (preset) {
    case WidgetFixedPreset::MinimalClock:
        return {L"极简时钟", 0.18f, 0.10f, kMinimalClockHtml};
    case WidgetFixedPreset::GlassClock:
        return {L"玻璃时钟", 0.30f, 0.20f, kGlassClockHtml};
    case WidgetFixedPreset::DateClock:
    default:
        return {L"日期时钟", 0.23f, 0.16f, kDateClockHtml};
    }
}

bool SameMonitor(const wallpaper::DesktopWidget& widget, std::wstring_view monitorId) {
    if (widget.monitorId.empty() && monitorId.empty()) return true;
    return _wcsicmp(widget.monitorId.c_str(), std::wstring(monitorId).c_str()) == 0;
}

std::pair<float, float> AutomaticPlacement(
    const std::vector<wallpaper::DesktopWidget>& widgets,
    std::wstring_view monitorId,
    float width,
    float height) {
    std::size_t occupied = 0;
    for (const auto& widget : widgets) {
        if (widget.enabled && SameMonitor(widget, monitorId)) ++occupied;
    }

    const float rowStep = height + kPlacementGap;
    const int rows = std::max(1, static_cast<int>((1.0f - 2.0f * kPlacementMargin + kPlacementGap) / rowStep));
    const int row = static_cast<int>(occupied % static_cast<std::size_t>(rows));
    const int column = static_cast<int>(occupied / static_cast<std::size_t>(rows));
    const float x = std::max(kPlacementMargin,
        1.0f - kPlacementMargin - width - column * (width + kPlacementGap));
    const float y = std::min(1.0f - kPlacementMargin - height,
        kPlacementMargin + row * rowStep);
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

DesktopControlResult DesktopWidgetController::CreatePreset(
    WidgetFixedPreset preset,
    std::wstring monitorId,
    wallpaper::DesktopWidget* created) const {
    std::vector<wallpaper::DesktopWidget> existing;
    const auto listed = service_.ListWidgets(&existing);
    if (!listed.success) return listed;

    const auto spec = PresetSpec(preset);
    const auto [x, y] = AutomaticPlacement(existing, monitorId, spec.width, spec.height);
    WebWidgetCreateRequest request;
    request.title = spec.title;
    request.htmlUtf8.assign(spec.html.begin(), spec.html.end());
    request.monitorId = std::move(monitorId);
    request.x = x;
    request.y = y;
    request.width = spec.width;
    request.height = spec.height;
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
