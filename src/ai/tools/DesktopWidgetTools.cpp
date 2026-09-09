#include "miaodesk/DesktopWidgetTools.h"

#include "miaodesk/DesktopControlService.h"

#include <windows.h>

#include <cctype>
#include <cmath>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace miaodesk {
namespace {

NativeToolResult ToNative(desktop::DesktopControlResult result) {
    return {result.success, std::move(result.message)};
}

const desktop::WidgetSurfaceHealth* FindSurfaceHealth(
    const desktop::WidgetRuntimeHealth& health,
    std::wstring_view widgetId) {
    for (const auto& surface : health.surfaces) {
        if (surface.widgetId == widgetId) return &surface;
    }
    return nullptr;
}

void AppendSurfaceHealth(std::wostringstream& text, const desktop::WidgetSurfaceHealth& surface) {
    text << L"\r\n  surface id=" << surface.widgetId
         << L"; rendering=" << (surface.renderingHealthy ? L"healthy" : L"attention")
         << L"; pid=" << surface.processId
         << L"; hwnd=" << surface.hwndValue
         << L"; parent=" << (surface.parentValid ? L"ok" : L"bad")
         << L"; child_style=" << (surface.childStyleValid ? L"ok" : L"bad")
         << L"; visible=" << (surface.visible ? L"true" : L"false")
         << L"; monitor=" << (surface.monitorId.empty() ? L"unresolved" : surface.monitorId)
         << L"; monitor_state=" << (surface.monitorReported ? (surface.monitorValid ? L"ok" : L"bad") : L"unreported")
         << L"; geometry=" << (surface.geometryReported ? (surface.geometryValid ? L"ok" : L"bad") : L"unreported")
         << L"; expected_rect=" << surface.expectedLeft << L"," << surface.expectedTop << L"," << surface.expectedRight << L"," << surface.expectedBottom
         << L"; actual_rect=" << surface.actualLeft << L"," << surface.actualTop << L"," << surface.actualRight << L"," << surface.actualBottom
         << L"; zorder=" << (surface.zOrderReported ? (surface.zOrderValid ? L"ok" : L"bad") : L"unreported");
    if (!surface.issueCode.empty()) text << L"; issue=" << surface.issueCode;
    if (!surface.detail.empty()) text << L"; detail=" << surface.detail;
    if (!surface.recommendedAction.empty()) text << L"; action=" << surface.recommendedAction;
}

NativeToolResult WallpaperStateGet() {
    desktop::DesktopControlService service;
    desktop::DesktopSnapshot snapshot;
    const auto result = service.GetSnapshot(&snapshot);
    if (!result.success) return ToNative(result);
    const auto& state = snapshot.desktop;

    std::wostringstream text;
    text << L"当前桌面状态：scene=" << state.scene
         << L"; layout=" << state.layout
         << L"; scale=" << state.scale
         << L"; fps=" << state.fpsCap
         << L"; widgets=" << state.widgetCount
         << L"; widget_runtime=" << (snapshot.widgetRuntime.Healthy() ? L"healthy" : L"attention")
         << L"; widget_enabled=" << snapshot.widgetRuntime.enabledCount
         << L"; widget_surfaces=" << snapshot.widgetRuntime.surfaces.size();
    if (!state.imageOrWebSource.empty()) text << L"; image/web=" << state.imageOrWebSource;
    if (!state.videoSource.empty()) text << L"; video=" << state.videoSource;
    if (!snapshot.widgetRuntime.detail.empty()) text << L"; widget_detail=" << snapshot.widgetRuntime.detail;
    for (const auto& surface : snapshot.widgetRuntime.surfaces) AppendSurfaceHealth(text, surface);
    return {true, text.str()};
}

NativeToolResult WidgetList() {
    desktop::DesktopControlService service;
    desktop::DesktopSnapshot snapshot;
    const auto result = service.GetSnapshot(&snapshot);
    if (!result.success) return ToNative(result);

    std::wostringstream text;
    text << L"桌面小组件：" << snapshot.widgets.size();
    for (const auto& widget : snapshot.widgets) {
        text << L"\r\n- id=" << widget.id << L"; title=" << widget.title
             << L"; enabled=" << (widget.enabled ? L"true" : L"false")
             << L"; monitor=" << (widget.monitorId.empty() ? L"primary" : widget.monitorId)
             << L"; rect=" << widget.x << L"," << widget.y << L"," << widget.width << L"," << widget.height;
        if (const auto* surface = FindSurfaceHealth(snapshot.widgetRuntime, widget.id)) {
            text << L"; runtime=" << (surface->renderingHealthy ? L"healthy" : L"attention")
                 << L"; resolved_monitor=" << (surface->monitorId.empty() ? L"unresolved" : surface->monitorId)
                 << L"; monitor_state=" << (surface->monitorReported ? (surface->monitorValid ? L"ok" : L"bad") : L"unreported")
                 << L"; geometry=" << (surface->geometryReported ? (surface->geometryValid ? L"ok" : L"bad") : L"unreported")
                 << L"; expected_rect=" << surface->expectedLeft << L"," << surface->expectedTop << L"," << surface->expectedRight << L"," << surface->expectedBottom
                 << L"; actual_rect=" << surface->actualLeft << L"," << surface->actualTop << L"," << surface->actualRight << L"," << surface->actualBottom;
            if (!surface->issueCode.empty()) text << L"; issue=" << surface->issueCode;
            if (!surface->recommendedAction.empty()) text << L"; action=" << surface->recommendedAction;
        }
    }
    return {true, text.str()};
}

} // namespace

NativeToolResult ExecuteDesktopControlTool(std::string_view toolName, std::string_view /*argumentsJson*/) {
    if (toolName == "wallpaper_state_get") return WallpaperStateGet();
    if (toolName == "desktop_widget_list") return WidgetList();
    return {false, L"未知桌面控制工具。"};
}

} // namespace miaodesk