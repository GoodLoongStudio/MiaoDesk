#include "miaodesk/DesktopWidgetController.h"
#include "miaodesk/MiaoWidgetContentCatalog.h"
#include "miaodesk/NativeWidgetPreset.h"
#include "miaodesk/RuntimeLogPaths.h"
#include "miaodesk/RuntimeLogger.h"
#include "miaodesk/WallpaperMonitorLayout.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <utility>

namespace miaodesk::desktop {
namespace {

const wchar_t* BoolText(bool value) noexcept { return value ? L"true" : L"false"; }

const wchar_t* WidgetKindText(wallpaper::DesktopWidgetKind kind) noexcept {
    switch (kind) {
    case wallpaper::DesktopWidgetKind::Native: return L"native";
    case wallpaper::DesktopWidgetKind::Content: return L"content";
    case wallpaper::DesktopWidgetKind::Unknown:
    default: return L"unknown";
    }
}

struct NormalizedRect {
    float left;
    float top;
    float right;
    float bottom;
};

constexpr float kPlacementMargin = 0.03f;
constexpr float kPlacementGap = 0.025f;
constexpr std::wstring_view kGlassClockDefinitionId = L"com.goodloong.glass-clock";
constexpr std::wstring_view kWeatherGlassDefinitionId = L"com.goodloong.weather-glass";

std::wstring_view ContentDefinitionIdForPreset(WidgetFixedPreset preset) noexcept {
    switch (preset) {
    case WidgetFixedPreset::GlassClock: return kGlassClockDefinitionId;
    case WidgetFixedPreset::WeatherGlass: return kWeatherGlassDefinitionId;
    case WidgetFixedPreset::TodayTasks:
    default: return {};
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

std::optional<std::pair<float, float>> AutomaticPlacement(
    const std::vector<wallpaper::DesktopWidget>& widgets,
    std::wstring_view monitorId,
    float width,
    float height) {
    const float maxX = std::max(kPlacementMargin, 1.0f - kPlacementMargin - width);
    const float maxY = std::max(kPlacementMargin, 1.0f - kPlacementMargin - height);
    const int xSteps = std::max(0, static_cast<int>(std::ceil((maxX - kPlacementMargin) / kPlacementGap)));
    const int ySteps = std::max(0, static_cast<int>(std::ceil((maxY - kPlacementMargin) / kPlacementGap)));

    for (int xStep = 0; xStep <= xSteps; ++xStep) {
        const float x = std::max(kPlacementMargin, maxX - static_cast<float>(xStep) * kPlacementGap);
        for (int yStep = 0; yStep <= ySteps; ++yStep) {
            const float y = std::min(maxY, kPlacementMargin + static_cast<float>(yStep) * kPlacementGap);
            const NormalizedRect candidate{x, y, x + width, y + height};
            if (PlacementFree(widgets, monitorId, candidate)) return std::pair{x, y};
        }
    }
    return std::nullopt;
}

std::wstring CanonicalMonitorId(std::wstring monitorId) {
    if (monitorId.empty()) return monitorId;
    const auto topology = wallpaper::QueryMonitorTopology();
    if (!topology.Valid()) return monitorId;
    const wallpaper::MonitorInfo* primary = nullptr;
    for (const auto& monitor : topology.monitors) {
        if (monitor.primary) { primary = &monitor; break; }
    }
    if (!primary && !topology.monitors.empty()) primary = &topology.monitors.front();
    if (!primary) return monitorId;
    const auto key = wallpaper::StableMonitorKey(*primary);
    if (_wcsicmp(key.c_str(), monitorId.c_str()) == 0 ||
        _wcsicmp(primary->deviceName.c_str(), monitorId.c_str()) == 0) return {};
    return monitorId;
}

bool MatchesWidgetPreset(const wallpaper::DesktopWidget& widget,
                         wallpaper::NativeWidgetPreset preset,
                         std::wstring_view monitorId) {
    if (!SameMonitor(widget, monitorId)) return false;
    if (widget.kind == wallpaper::DesktopWidgetKind::Native) {
        wallpaper::NativeWidgetPreset existing{};
        return wallpaper::ParseNativePreset(widget.source.wstring(), &existing) && existing == preset;
    }
    if (widget.kind == wallpaper::DesktopWidgetKind::Content) {
        const auto definitionId = ContentDefinitionIdForPreset(preset);
        if (definitionId.empty()) return false;
        return widget.source.wstring() == content::MiaoWidgetContentCatalog::MakeSource(definitionId);
    }
    return false;
}

void AppendUtf8Log(const std::filesystem::path& path, std::wstring_view text) {
    if (path.empty() || text.empty()) return;
    const std::string utf8 = miaodesk::log::WideToUtf8(text);
    if (utf8.empty()) return;
    HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    CloseHandle(file);
}

void AppendControllerErrorLog(std::wstring_view message) {
    const auto path = miaodesk::RuntimeLogPath(L"widget-runtime.log");
    if (path.empty()) return;
    std::wostringstream log;
    SYSTEMTIME now{};
    GetLocalTime(&now);
    log << L"\n=== "
        << std::setfill(L'0') << std::setw(4) << now.wYear << L'-'
        << std::setw(2) << now.wMonth << L'-' << std::setw(2) << now.wDay << L' '
        << std::setw(2) << now.wHour << L':' << std::setw(2) << now.wMinute << L':'
        << std::setw(2) << now.wSecond << L" Widget controller error ===\n"
        << message << L"\n";
    AppendUtf8Log(path, log.str());
}

void AppendWidgetRuntimeLog(const DesktopSnapshot& snapshot) {
    const auto path = miaodesk::RuntimeLogPath(L"widget-runtime.log");
    if (path.empty()) return;
    std::wostringstream log;

    SYSTEMTIME now{};
    GetLocalTime(&now);
    log << L"\n=== "
        << std::setfill(L'0') << std::setw(4) << now.wYear << L'-'
        << std::setw(2) << now.wMonth << L'-' << std::setw(2) << now.wDay << L' '
        << std::setw(2) << now.wHour << L':' << std::setw(2) << now.wMinute << L':'
        << std::setw(2) << now.wSecond << L" Widget snapshot ===\n";

    log << L"wallpaper.enabled=" << BoolText(snapshot.desktop.enabled)
        << L" wallpaper.scene=" << snapshot.desktop.scene
        << L" wallpaper.layout=" << snapshot.desktop.layout
        << L" configuredWidgets=" << snapshot.widgets.size()
        << L" enabled=" << snapshot.widgetRuntime.enabledCount
        << L" runtimeReported=" << BoolText(snapshot.widgetRuntime.runtimeReported)
        << L" runtimeHealthy=" << BoolText(snapshot.widgetRuntime.runtimeHealthy)
        << L" detail=" << snapshot.widgetRuntime.detail << L"\n";

    for (const auto& widget : snapshot.widgets) {
        log << L"config id=" << widget.id << L" title=\"" << widget.title << L"\""
            << L" enabled=" << BoolText(widget.enabled)
            << L" kind=" << WidgetKindText(widget.kind)
            << L" monitor=\"" << (widget.monitorId.empty() ? L"<primary>" : widget.monitorId) << L"\""
            << L" x=" << widget.x << L" y=" << widget.y
            << L" width=" << widget.width << L" height=" << widget.height
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
            << L" zOrder=" << BoolText(surface.zOrderReported) << L'/' << BoolText(surface.zOrderValid)
            << L" renderingHealthy=" << BoolText(surface.renderingHealthy)
            << L" issue=\"" << surface.issueCode << L"\""
            << L" detail=\"" << surface.detail << L"\""
            << L" action=\"" << surface.recommendedAction << L"\"\n";
    }
    AppendUtf8Log(path, log.str());
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
    monitorId = CanonicalMonitorId(std::move(monitorId));
    std::vector<wallpaper::DesktopWidget> existing;
    const auto listed = service_.ListWidgets(&existing);
    if (!listed.success) return listed;

    constexpr std::array<WidgetFixedPreset, 3> order{
        WidgetFixedPreset::GlassClock,
        WidgetFixedPreset::TodayTasks,
        WidgetFixedPreset::WeatherGlass,
    };

    for (const auto preset : order) {
        const bool exists = std::any_of(existing.begin(), existing.end(), [&](const auto& widget) {
            return MatchesWidgetPreset(widget, preset, monitorId);
        });
        if (!exists) return CreatePreset(preset, monitorId, created);
    }
    for (const auto preset : order) {
        auto disabled = std::find_if(existing.begin(), existing.end(), [&](const auto& widget) {
            return MatchesWidgetPreset(widget, preset, monitorId) && !widget.enabled;
        });
        if (disabled != existing.end()) return CreatePreset(preset, monitorId, created);
    }
    return {false, L"该显示器的玻璃时钟、今日待办和玻璃天气均已存在；请拖动、停用或删除现有组件。"};
}

DesktopControlResult DesktopWidgetController::CreatePreset(
    WidgetFixedPreset preset,
    std::wstring monitorId,
    wallpaper::DesktopWidget* created) const {
    monitorId = CanonicalMonitorId(std::move(monitorId));
    std::vector<wallpaper::DesktopWidget> existing;
    const auto listed = service_.ListWidgets(&existing);
    if (!listed.success) return listed;

    const auto* definition = wallpaper::NativePresetDefinition(preset);
    if (!definition) return {false, L"未知的小组件模板。"};

    auto duplicate = std::find_if(existing.begin(), existing.end(), [&](const auto& widget) {
        return MatchesWidgetPreset(widget, preset, monitorId);
    });
    if (duplicate != existing.end()) {
        if (created) *created = *duplicate;
        if (!duplicate->enabled) {
            const auto enabled = SetEnabled(duplicate->id, true);
            if (enabled.success && created) created->enabled = true;
            return enabled.success
                ? DesktopControlResult{true, L"已重新启用现有「" + std::wstring(definition->title) + L"」。"}
                : enabled;
        }
        return {false, L"该显示器已经存在「" + std::wstring(definition->title) + L"」，不会重复创建重叠副本。"};
    }

    float placementWidth = definition->defaultWidth;
    float placementHeight = definition->defaultHeight;
    const auto contentDefinitionId = ContentDefinitionIdForPreset(preset);
    bool useContent = false;
    if (!contentDefinitionId.empty()) {
        const auto source = content::MiaoWidgetContentCatalog::MakeSource(contentDefinitionId);
        content::ResolvedWidgetContent resolved;
        std::wstring error;
        if (content::MiaoWidgetContentCatalog::Resolve(source, &resolved, &error) &&
            resolved.definition.runtime == content::ContentRuntimeKind::Scene) {
            placementWidth = resolved.definition.geometry.defaultWidth;
            placementHeight = resolved.definition.geometry.defaultHeight;
            useContent = true;
        } else {
            miaodesk::log::Info(
                L"WidgetController",
                std::wstring(definition->title) + L" Content package unavailable; using native fallback" +
                    (error.empty() ? std::wstring{} : L": " + error));
        }
    }

    const auto placement = AutomaticPlacement(existing, monitorId, placementWidth, placementHeight);
    if (!placement) {
        return {false, L"当前显示器没有足够的空闲区域放置「" + std::wstring(definition->title) + L"」；请先移动或删除现有小组件。"};
    }

    if (useContent) {
        ContentWidgetCreateRequest request;
        request.definitionId = std::wstring(contentDefinitionId);
        request.title = std::wstring(definition->title);
        request.monitorId = std::move(monitorId);
        request.x = placement->first;
        request.y = placement->second;
        request.enabled = true;
        return service_.CreateContentWidget(request, created);
    }

    NativeWidgetCreateRequest request;
    request.preset = preset;
    request.title = std::wstring(definition->title);
    request.monitorId = std::move(monitorId);
    request.x = placement->first;
    request.y = placement->second;
    request.width = definition->defaultWidth;
    request.height = definition->defaultHeight;
    return service_.CreateNativeWidget(request, created);
}

DesktopControlResult DesktopWidgetController::GetContentParameters(
    std::wstring_view id,
    content::ContentParameterValues* values) const {
    return service_.GetContentWidgetParameters(id, values);
}

DesktopControlResult DesktopWidgetController::SetContentParameter(
    std::wstring_view id,
    std::wstring_view key,
    content::ContentParameterValue value) const {
    return service_.SetContentWidgetParameter(id, key, std::move(value));
}

DesktopControlResult DesktopWidgetController::ResetContentParameters(std::wstring_view id) const {
    return service_.ResetContentWidgetParameters(id);
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

} // namespace miaodesk::desktop
