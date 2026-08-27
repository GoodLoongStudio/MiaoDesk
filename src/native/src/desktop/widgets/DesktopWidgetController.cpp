#include "turingdesk/DesktopWidgetController.h"
#include "turingdesk/RuntimeLogPaths.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
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

struct NormalizedRect {
    float left;
    float top;
    float right;
    float bottom;
};

constexpr float kPlacementMargin = 0.03f;
constexpr float kPlacementGap = 0.025f;

constexpr std::string_view kMinimalClockHtml = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><style>
html,body{margin:0;width:100%;height:100%;overflow:hidden;background:transparent;font-family:"Segoe UI Variable Text","Segoe UI",sans-serif;color:#fff}
.card{box-sizing:border-box;width:100%;height:100%;position:relative;display:flex;align-items:center;justify-content:center;border-radius:24px;overflow:hidden;background:linear-gradient(125deg,rgba(9,13,24,.92),rgba(23,25,44,.82));border:1px solid rgba(255,255,255,.16);box-shadow:0 16px 40px rgba(0,0,0,.34);backdrop-filter:blur(22px)}
.card:before{content:"";position:absolute;inset:-80%;background:conic-gradient(from 90deg,transparent,#56f6d2 10%,transparent 24%,#8a64ff 42%,transparent 57%,#46baff 72%,transparent 86%);opacity:.28;animation:spin 12s linear infinite}.inner{position:relative;z-index:1;display:flex;align-items:baseline;gap:8px;text-shadow:0 0 24px rgba(112,207,255,.24)}
#time{font-size:clamp(34px,20vw,62px);font-weight:680;letter-spacing:-2px;line-height:1}#sec{font-size:clamp(11px,5vw,17px);font-variant-numeric:tabular-nums;opacity:.55}
@keyframes spin{to{transform:rotate(360deg)}}
</style></head><body><div class="card"><div class="inner"><div id="time"></div><div id="sec"></div></div></div><script>
function tick(){const d=new Date();document.getElementById('time').textContent=d.toLocaleTimeString([], {hour:'2-digit',minute:'2-digit'});document.getElementById('sec').textContent=String(d.getSeconds()).padStart(2,'0');}tick();setInterval(tick,1000);
</script></body></html>)HTML";

constexpr std::string_view kDateClockHtml = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><style>
html,body{margin:0;width:100%;height:100%;overflow:hidden;background:transparent;font-family:"Segoe UI Variable Text","Segoe UI",sans-serif;color:white}
.card{box-sizing:border-box;width:100%;height:100%;position:relative;overflow:hidden;display:flex;flex-direction:column;justify-content:flex-end;padding:34px 22px 18px;border-radius:28px;background:linear-gradient(145deg,rgba(14,18,34,.92),rgba(29,20,48,.82));border:1px solid rgba(255,255,255,.16);box-shadow:0 18px 44px rgba(0,0,0,.34);backdrop-filter:blur(24px)}
.orb{position:absolute;border-radius:999px;filter:blur(22px);opacity:.48;animation:float 7s ease-in-out infinite alternate}.a{width:55%;aspect-ratio:1;right:-12%;top:-28%;background:#5d7cff}.b{width:42%;aspect-ratio:1;left:-14%;bottom:-26%;background:#00d8c0;animation-delay:-3s}.eyebrow{position:relative;font-size:10px;font-weight:700;letter-spacing:1.8px;opacity:.6;text-transform:uppercase}.time{position:relative;font-size:clamp(32px,14vw,58px);font-weight:700;letter-spacing:-1.8px;line-height:1;margin-top:5px}.date{position:relative;margin-top:10px;font-size:clamp(12px,5vw,17px);opacity:.78;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
@keyframes float{to{transform:translate(10px,12px) scale(1.08)}}
</style></head><body><div class="card"><div class="orb a"></div><div class="orb b"></div><div class="eyebrow">MIAO · TODAY</div><div class="time" id="time"></div><div class="date" id="date"></div></div><script>
function tick(){const d=new Date();document.getElementById('time').textContent=d.toLocaleTimeString([], {hour:'2-digit',minute:'2-digit'});document.getElementById('date').textContent=d.toLocaleDateString([], {weekday:'long',year:'numeric',month:'long',day:'numeric'});}tick();setInterval(tick,1000);
</script></body></html>)HTML";

constexpr std::string_view kGlassClockHtml = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><style>
html,body{margin:0;width:100%;height:100%;overflow:hidden;background:transparent;font-family:"Segoe UI Variable Text","Segoe UI",sans-serif;color:white}
.card{box-sizing:border-box;width:100%;height:100%;position:relative;overflow:hidden;display:flex;flex-direction:column;justify-content:flex-end;padding:38px 26px 24px;border:1px solid rgba(255,255,255,.23);border-radius:32px;background:linear-gradient(135deg,rgba(255,255,255,.16),rgba(255,255,255,.045));box-shadow:0 22px 52px rgba(0,0,0,.32),inset 0 1px rgba(255,255,255,.18);backdrop-filter:blur(30px)}
.aurora{position:absolute;inset:-35%;background:radial-gradient(circle at 28% 42%,rgba(60,255,205,.52),transparent 28%),radial-gradient(circle at 72% 35%,rgba(92,100,255,.62),transparent 30%),radial-gradient(circle at 55% 75%,rgba(220,69,255,.45),transparent 28%);filter:blur(16px);animation:drift 10s ease-in-out infinite alternate}.mesh{position:absolute;inset:0;background-image:linear-gradient(rgba(255,255,255,.035) 1px,transparent 1px),linear-gradient(90deg,rgba(255,255,255,.035) 1px,transparent 1px);background-size:28px 28px;mask-image:linear-gradient(to bottom,rgba(0,0,0,.75),transparent 75%)}
.chip{position:absolute;left:22px;top:22px;padding:5px 9px;border-radius:999px;background:rgba(5,10,22,.28);border:1px solid rgba(255,255,255,.14);font-size:9px;font-weight:750;letter-spacing:1.6px;opacity:.76}.time{position:relative;font-size:clamp(42px,14vw,78px);font-weight:660;letter-spacing:-2.8px;line-height:.94;text-shadow:0 4px 30px rgba(0,0,0,.2)}.date{position:relative;margin-top:13px;font-size:clamp(13px,4vw,18px);opacity:.84}
.card:after{content:"";position:absolute;left:-38%;top:-55%;width:42%;height:220%;background:linear-gradient(90deg,transparent,rgba(255,255,255,.14),rgba(121,240,255,.20),transparent);transform:rotate(19deg);mix-blend-mode:screen;animation:sweep 7.5s ease-in-out infinite}.chip:before{content:"";display:inline-block;width:6px;height:6px;margin-right:6px;border-radius:50%;background:#64ffe0;box-shadow:0 0 12px #64ffe0;vertical-align:1px}.mesh{animation:meshFloat 8s ease-in-out infinite alternate}.time{filter:drop-shadow(0 0 18px rgba(118,215,255,.24))}
@keyframes drift{0%{transform:translate(-3%,-2%) rotate(-4deg) scale(1)}100%{transform:translate(4%,3%) rotate(5deg) scale(1.08)}}@keyframes sweep{0%,18%{transform:translateX(-20%) rotate(19deg);opacity:0}45%{opacity:1}72%,100%{transform:translateX(360%) rotate(19deg);opacity:0}}@keyframes meshFloat{to{transform:translate3d(10px,7px,0)}}
</style></head><body><div class="card"><div class="aurora"></div><div class="mesh"></div><div class="chip">MIAO · DESKTOP</div><div class="time" id="time"></div><div class="date" id="date"></div></div><script>
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

NormalizedRect WidgetRect(const wallpaper::DesktopWidget& widget) noexcept {
    return {widget.x, widget.y, widget.x + widget.width, widget.y + widget.height};
}

bool IntersectsWithGap(const NormalizedRect& candidate, const NormalizedRect& occupied) noexcept {
    return !(candidate.right + kPlacementGap <= occupied.left ||
             occupied.right + kPlacementGap <= candidate.left ||
             candidate.bottom + kPlacementGap <= occupied.top ||
             occupied.bottom + kPlacementGap <= candidate.top);
}

bool PlacementFree(
    const std::vector<wallpaper::DesktopWidget>& widgets,
    std::wstring_view monitorId,
    const NormalizedRect& candidate) {
    for (const auto& widget : widgets) {
        if (!widget.enabled || !SameMonitor(widget, monitorId)) continue;
        if (IntersectsWithGap(candidate, WidgetRect(widget))) return false;
    }
    return true;
}

std::pair<float, float> AutomaticPlacement(
    const std::vector<wallpaper::DesktopWidget>& widgets,
    std::wstring_view monitorId,
    float width,
    float height) {
    // Fixed M3 presets can have different sizes. Scan right-to-left and
    // top-to-bottom in logical desktop space and reject any candidate that
    // intersects an enabled Widget on the same monitor. This keeps the showcase
    // deterministic before the user freely drags the Widget to a preferred position.
    const float maxX = std::max(kPlacementMargin, 1.0f - kPlacementMargin - width);
    const float maxY = std::max(kPlacementMargin, 1.0f - kPlacementMargin - height);
    const int xSteps = std::max(0, static_cast<int>(std::ceil((maxX - kPlacementMargin) / kPlacementGap)));
    const int ySteps = std::max(0, static_cast<int>(std::ceil((maxY - kPlacementMargin) / kPlacementGap)));

    for (int xStep = 0; xStep <= xSteps; ++xStep) {
        const float x = std::max(kPlacementMargin, maxX - static_cast<float>(xStep) * kPlacementGap);
        for (int yStep = 0; yStep <= ySteps; ++yStep) {
            const float y = std::min(maxY, kPlacementMargin + static_cast<float>(yStep) * kPlacementGap);
            const NormalizedRect candidate{x, y, x + width, y + height};
            if (PlacementFree(widgets, monitorId, candidate)) return {x, y};
        }
    }

    // A crowded desktop should still allow creation; use the canonical top-right
    // slot as a deterministic fallback rather than exposing coordinates to the user.
    return {maxX, kPlacementMargin};
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
    std::vector<wallpaper::DesktopWidget> existing;
    const auto listed = service_.ListWidgets(&existing);
    if (!listed.success) return listed;

    std::size_t minimalCount = 0;
    std::size_t dateCount = 0;
    std::size_t glassCount = 0;
    for (const auto& widget : existing) {
        if (_wcsicmp(widget.title.c_str(), L"极简时钟") == 0) ++minimalCount;
        else if (_wcsicmp(widget.title.c_str(), L"日期时钟") == 0) ++dateCount;
        else if (_wcsicmp(widget.title.c_str(), L"玻璃时钟") == 0) ++glassCount;
    }

    // The flagship glass preset wins ties, so the very first built-in Widget
    // showcases the richer desktop visual instead of the plain clock.
    WidgetFixedPreset preset = WidgetFixedPreset::GlassClock;
    if (dateCount < glassCount && dateCount <= minimalCount) preset = WidgetFixedPreset::DateClock;
    else if (minimalCount < glassCount && minimalCount < dateCount) preset = WidgetFixedPreset::MinimalClock;

    return CreatePreset(preset, std::move(monitorId), created);
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
