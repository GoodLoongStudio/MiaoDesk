#include "turingdesk/DesktopWidgetController.h"
#include "turingdesk/NativeWidgetPreset.h"
#include "turingdesk/RuntimeLogPaths.h"
#include "turingdesk/WallpaperMonitorLayout.h"

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

constexpr std::string_view kTodayTasksHtml = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><style>
html,body{margin:0;width:100%;height:100%;overflow:hidden;background:transparent;font-family:"Segoe UI Variable Text","Segoe UI",sans-serif;color:#fff}
.card{box-sizing:border-box;width:100%;height:100%;position:relative;overflow:hidden;display:flex;flex-direction:column;padding:20px 18px 16px;border-radius:26px;background:linear-gradient(145deg,rgba(12,18,36,.94),rgba(22,28,52,.88));border:1px solid rgba(255,255,255,.15);box-shadow:0 18px 44px rgba(0,0,0,.34)}
.glow{position:absolute;right:-18%;top:-30%;width:55%;aspect-ratio:1;border-radius:50%;background:radial-gradient(circle,rgba(86,246,210,.28),transparent 68%)}
.head{position:relative;display:flex;align-items:center;justify-content:space-between;margin-bottom:12px}
.title{font-size:11px;font-weight:750;letter-spacing:1.6px;opacity:.72;text-transform:uppercase}.badge{padding:4px 8px;border-radius:999px;background:rgba(100,255,224,.14);border:1px solid rgba(100,255,224,.28);font-size:10px;font-weight:700;color:#8dffe8}
.list{position:relative;flex:1;display:flex;flex-direction:column;gap:8px}
.item{display:flex;align-items:center;gap:10px;padding:9px 10px;border-radius:14px;background:rgba(255,255,255,.06);border:1px solid rgba(255,255,255,.08)}
.dot{width:8px;height:8px;border-radius:50%;background:linear-gradient(135deg,#56f6d2,#8a64ff);flex-shrink:0}
.text{font-size:clamp(12px,3.8vw,15px);line-height:1.25;opacity:.92}
.item.done .text{opacity:.48;text-decoration:line-through}
</style></head><body><div class="card"><div class="glow"></div><div class="head"><div class="title">MIAO · 今日待办</div><div class="badge">3 项</div></div><div class="list" id="list"></div></div><script>
const tasks=["整理桌面","完成预览","提交版本"];const list=document.getElementById('list');tasks.forEach((t,i)=>{const el=document.createElement('div');el.className='item'+(i===2?' done':'');el.innerHTML='<div class="dot"></div><div class="text">'+t+'</div>';list.appendChild(el);});
</script></body></html>)HTML";

constexpr std::string_view kWeatherGlassHtml = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><style>
html,body{margin:0;width:100%;height:100%;overflow:hidden;background:transparent;font-family:"Segoe UI Variable Text","Segoe UI",sans-serif;color:#fff}
.card{box-sizing:border-box;width:100%;height:100%;position:relative;overflow:hidden;display:flex;flex-direction:column;justify-content:space-between;padding:22px 20px 18px;border-radius:28px;background:linear-gradient(135deg,rgba(18,42,74,.92),rgba(28,58,96,.86));border:1px solid rgba(255,255,255,.18);box-shadow:0 20px 48px rgba(0,0,0,.30)}
.sky{position:absolute;inset:0;background:radial-gradient(circle at 30% 35%,rgba(70,186,255,.22),transparent 36%),radial-gradient(circle at 72% 28%,rgba(137,196,255,.16),transparent 34%)}
.loc{position:relative;font-size:10px;font-weight:700;letter-spacing:1.4px;opacity:.68;text-transform:uppercase}
.main{position:relative;display:flex;align-items:flex-end;justify-content:space-between;margin-top:8px}
.temp{font-size:clamp(38px,16vw,64px);font-weight:680;letter-spacing:-2px;line-height:.9}
.cond{text-align:right;font-size:clamp(13px,4vw,17px);opacity:.86;line-height:1.35}
.forecast{position:relative;display:flex;gap:8px;margin-top:10px}
.day{flex:1;padding:8px 6px;border-radius:14px;background:rgba(5,10,22,.22);border:1px solid rgba(255,255,255,.10);text-align:center;font-size:10px;opacity:.82}
.day b{display:block;font-size:13px;margin-top:4px;font-weight:650}
</style></head><body><div class="card"><div class="sky"></div><div class="loc">MIAO · 本地天气</div><div class="main"><div class="temp" id="temp">22°</div><div class="cond" id="cond">晴朗<br>体感 24°</div></div><div class="forecast" id="forecast"></div></div><script>
const days=[{d:'今天',t:'22°'},{d:'明天',t:'20°'},{d:'后天',t:'18°'}];const fc=document.getElementById('forecast');days.forEach(x=>{const el=document.createElement('div');el.className='day';el.innerHTML=x.d+'<b>'+x.t+'</b>';fc.appendChild(el);});
</script></body></html>)HTML";

constexpr std::string_view kGlassClockHtml = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><style>
html,body{margin:0;width:100%;height:100%;overflow:hidden;background:transparent;font-family:"Segoe UI Variable Text","Segoe UI",sans-serif;color:white}
.card{box-sizing:border-box;width:100%;height:100%;position:relative;overflow:hidden;display:flex;flex-direction:column;justify-content:flex-end;padding:38px 26px 24px;border:1px solid rgba(255,255,255,.20);border-radius:32px;background:linear-gradient(135deg,rgba(20,36,58,.92),rgba(28,48,78,.86));box-shadow:0 22px 52px rgba(0,0,0,.32)}
.aurora{position:absolute;inset:0;background:radial-gradient(circle at 28% 42%,rgba(60,255,205,.18),transparent 34%),radial-gradient(circle at 72% 35%,rgba(92,100,255,.20),transparent 32%)}
.chip{position:absolute;left:22px;top:22px;padding:5px 9px;border-radius:999px;background:rgba(5,10,22,.35);border:1px solid rgba(255,255,255,.14);font-size:9px;font-weight:750;letter-spacing:1.6px;opacity:.76}
.time{position:relative;font-size:clamp(42px,14vw,78px);font-weight:660;letter-spacing:-2.8px;line-height:.94}
.date{position:relative;margin-top:13px;font-size:clamp(13px,4vw,18px);opacity:.84}
.chip:before{content:"";display:inline-block;width:6px;height:6px;margin-right:6px;border-radius:50%;background:#64ffe0;vertical-align:1px}
</style></head><body><div class="card"><div class="aurora"></div><div class="chip">MIAO · DESKTOP</div><div class="time" id="time"></div><div class="date" id="date"></div></div><script>
function tick(){const d=new Date();const t=document.getElementById('time');const dt=document.getElementById('date');const h=d.getHours().toString().padStart(2,'0');const m=d.getMinutes().toString().padStart(2,'0');const next=h+':'+m;if(t.textContent!==next){t.textContent=next;const ds=d.toLocaleDateString([],{weekday:'long',month:'long',day:'numeric'});if(dt.textContent!==ds)dt.textContent=ds;}}
tick();setInterval(tick,1000);
</script></body></html>)HTML";

FixedPresetSpec PresetSpec(WidgetFixedPreset preset) noexcept {
    switch (preset) {
    case WidgetFixedPreset::TodayTasks:
        return {L"今日待办", 0.26f, 0.24f, kTodayTasksHtml};
    case WidgetFixedPreset::WeatherGlass:
        return {L"玻璃天气", 0.24f, 0.20f, kWeatherGlassHtml};
    case WidgetFixedPreset::GlassClock:
    default:
        return {L"玻璃时钟", 0.30f, 0.20f, kGlassClockHtml};
    }
}

wallpaper::NativeWidgetPreset NativePresetFor(WidgetFixedPreset preset) noexcept {
    switch (preset) {
    case WidgetFixedPreset::TodayTasks: return wallpaper::NativeWidgetPreset::TodayTasks;
    case WidgetFixedPreset::WeatherGlass: return wallpaper::NativeWidgetPreset::WeatherGlass;
    case WidgetFixedPreset::GlassClock:
    default: return wallpaper::NativeWidgetPreset::GlassClock;
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
            << L" kind=" << (widget.kind == wallpaper::DesktopWidgetKind::Web ? L"web"
                : widget.kind == wallpaper::DesktopWidgetKind::Native ? L"native" : L"unknown")
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

    std::size_t glassCount = 0;
    std::size_t tasksCount = 0;
    std::size_t weatherCount = 0;
    for (const auto& widget : existing) {
        if (_wcsicmp(widget.title.c_str(), L"玻璃时钟") == 0) ++glassCount;
        else if (_wcsicmp(widget.title.c_str(), L"今日待办") == 0) ++tasksCount;
        else if (_wcsicmp(widget.title.c_str(), L"玻璃天气") == 0) ++weatherCount;
    }

    // The flagship glass clock wins ties so the first built-in Widget showcases
    // the richer desktop visual instead of tasks or weather.
    WidgetFixedPreset preset = WidgetFixedPreset::GlassClock;
    if (tasksCount < glassCount && tasksCount <= weatherCount) preset = WidgetFixedPreset::TodayTasks;
    else if (weatherCount < glassCount && weatherCount < tasksCount) preset = WidgetFixedPreset::WeatherGlass;

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
    if (!monitorId.empty()) {
        const auto topology = wallpaper::QueryMonitorTopology();
        if (topology.Valid()) {
            const wallpaper::MonitorInfo* primary = nullptr;
            for (const auto& monitor : topology.monitors) {
                if (monitor.primary) {
                    primary = &monitor;
                    break;
                }
            }
            if (!primary && !topology.monitors.empty()) primary = &topology.monitors.front();
            if (primary) {
                const auto key = wallpaper::StableMonitorKey(*primary);
                if (_wcsicmp(key.c_str(), monitorId.c_str()) == 0 ||
                    _wcsicmp(primary->deviceName.c_str(), monitorId.c_str()) == 0) {
                    monitorId.clear();
                }
            }
        }
    }
    NativeWidgetCreateRequest request;
    request.preset = NativePresetFor(preset);
    request.title = spec.title;
    request.monitorId = std::move(monitorId);
    request.x = x;
    request.y = y;
    request.width = spec.width;
    request.height = spec.height;
    return service_.CreateNativeWidget(request, created);
}

DesktopControlResult DesktopWidgetController::SetEnabled(std::wstring_view id, bool enabled) const {
    WidgetUpdateRequest request;
    request.id = std::wstring(id);
    request.enabled = enabled;
    return service_.UpdateWidget(request);
}

DesktopControlResult DesktopWidgetController::MoveTo(std::wstring_view id, float x, float y) const {
    wallpaper::DesktopWidget widget;
    const auto found = Find(id, &widget);
    if (!found.success) return found;

    const float maxX = std::max(0.0f, 1.0f - widget.width);
    const float maxY = std::max(0.0f, 1.0f - widget.height);
    WidgetUpdateRequest request;
    request.id = std::wstring(id);
    request.x = std::clamp(x, 0.0f, maxX);
    request.y = std::clamp(y, 0.0f, maxY);
    return service_.UpdateWidget(request);
}

DesktopControlResult DesktopWidgetController::Remove(std::wstring_view id) const {
    return service_.RemoveWidget(id);
}

} // namespace turingdesk::desktop
