#include "miaodesk/WidgetService.h"
#include "miaodesk/AppPaths.h"
#include "miaodesk/DesktopSurfaceTelemetry.h"
#include "miaodesk/NativeWidgetHost.h"
#include "miaodesk/WallpaperMonitorLayout.h"
#include "miaodesk/WebDesktopSurfaceChild.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iterator>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;

namespace miaodesk::desktop {
namespace {

constexpr wchar_t kNativeHostClass[] = L"MiaoDesk.Native.WidgetSurface";
constexpr LONG kGeometryTolerancePx = 8;
constexpr float kPresetGeometryTolerance = 0.0001f;

WidgetServiceResult LoadFailure(const std::wstring& error) {
    return {false, error.empty() ? L"无法读取桌面小组件状态。" : error};
}

fs::path WallpaperConfigPath() {
    return paths::StateFile(L"wallpaper.ini");
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
    return detail.find(L"Native Direct2D Widget host") != std::wstring::npos;
}

std::wstring WindowText(HWND window) {
    if (!window || !IsWindow(window)) return {};
    const int length = GetWindowTextLengthW(window);
    if (length <= 0) return {};
    std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
    const int copied = GetWindowTextW(window, text.data(), static_cast<int>(text.size()));
    if (copied <= 0) return {};
    text.resize(static_cast<std::size_t>(copied));
    return text;
}

bool WindowClassEquals(HWND window, const wchar_t* expected) {
    wchar_t className[256]{};
    return window && IsWindow(window) &&
           GetClassNameW(window, className, static_cast<int>(std::size(className))) > 0 &&
           _wcsicmp(className, expected) == 0;
}

bool ProcessRunning(DWORD processId) {
    if (processId == 0) return false;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, processId);
    if (!process) return true;
    DWORD exitCode = 0;
    const bool running = GetExitCodeProcess(process, &exitCode) && exitCode == STILL_ACTIVE;
    CloseHandle(process);
    return running;
}

bool PropertyReady(HWND window, const wchar_t* property) {
    return window && IsWindow(window) && GetPropW(window, property) != nullptr;
}

bool HasStructuredLifecycleTelemetry(HWND window) {
    if (!window || !IsWindow(window)) return false;
    const auto role = reinterpret_cast<INT_PTR>(GetPropW(window, wallpaper::kWebSurfaceRoleProperty));
    return role == 2;
}

const wallpaper::MonitorInfo* PrimaryMonitor(const wallpaper::MonitorTopology& topology) {
    for (const auto& monitor : topology.monitors) {
        if (monitor.primary) return &monitor;
    }
    return topology.monitors.empty() ? nullptr : &topology.monitors.front();
}

HWND FindWidgetSurface(HWND expectedParent, std::wstring_view widgetId) {
    if (widgetId.empty()) return nullptr;
    const std::wstring prefix = L"widget-" + std::wstring(widgetId) + L"-";
    auto consider = [&](HWND child, HWND& best) {
        if (!child || !IsWindow(child)) return;
        if (!WindowClassEquals(child, kNativeHostClass)) return;
        const auto title = WindowText(child);
        if (title.size() < prefix.size() || title.compare(0, prefix.size(), prefix) != 0) return;
        if (!best || ((IsWindowVisible(child) != FALSE) && IsWindowVisible(best) == FALSE)) best = child;
    };

    HWND best = nullptr;
    if (expectedParent && IsWindow(expectedParent)) {
        for (HWND child = GetWindow(expectedParent, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
            consider(child, best);
    }
    if (best) return best;

    for (HWND child = GetWindow(GetDesktopWindow(), GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
        consider(child, best);
    return best;
}

const wallpaper::MonitorInfo* ResolveWidgetMonitor(
    const wallpaper::MonitorTopology& topology,
    const wallpaper::DesktopWidget& widget) {
    if (!topology.Valid()) return nullptr;
    if (!widget.monitorId.empty()) {
        if (const auto* found = wallpaper::FindMonitorByStableId(topology, widget.monitorId)) return found;
        for (const auto& monitor : topology.monitors) {
            if (_wcsicmp(monitor.deviceName.c_str(), widget.monitorId.c_str()) == 0) return &monitor;
            if (!monitor.stableId.empty() && _wcsicmp(monitor.stableId.c_str(), widget.monitorId.c_str()) == 0)
                return &monitor;
        }
    }
    return PrimaryMonitor(topology);
}

bool NativeLifecycleReady(HWND window) {
    if (!window || !IsWindow(window)) return false;
    if (!HasStructuredLifecycleTelemetry(window)) return false;
    return PropertyReady(window, wallpaper::kWebSurfaceEnvironmentReadyProperty) &&
           PropertyReady(window, wallpaper::kWebSurfaceControllerReadyProperty) &&
           PropertyReady(window, wallpaper::kWebSurfaceNavigationReadyProperty) &&
           PropertyReady(window, wallpaper::kNativeWidgetPaintReadyProperty);
}

RECT ExpectedWidgetDesktopRect(const wallpaper::MonitorInfo& monitor, const wallpaper::DesktopWidget& widget) {
    const LONG monitorWidth = std::max<LONG>(1, monitor.desktopRect.right - monitor.desktopRect.left);
    const LONG monitorHeight = std::max<LONG>(1, monitor.desktopRect.bottom - monitor.desktopRect.top);
    RECT rect{};
    rect.left = monitor.desktopRect.left + static_cast<LONG>(std::lround(widget.x * monitorWidth));
    rect.top = monitor.desktopRect.top + static_cast<LONG>(std::lround(widget.y * monitorHeight));
    rect.right = rect.left + static_cast<LONG>(std::lround(widget.width * monitorWidth));
    rect.bottom = rect.top + static_cast<LONG>(std::lround(widget.height * monitorHeight));
    return rect;
}

bool RectNear(const RECT& expected, const RECT& actual) {
    const auto closeEnough = [](LONG a, LONG b) { return std::abs(a - b) <= kGeometryTolerancePx; };
    return closeEnough(expected.left, actual.left) && closeEnough(expected.top, actual.top) &&
           closeEnough(expected.right, actual.right) && closeEnough(expected.bottom, actual.bottom);
}

std::wstring RectText(const RECT& rect) {
    return L"[" + std::to_wstring(rect.left) + L"," + std::to_wstring(rect.top) + L"," +
           std::to_wstring(rect.right) + L"," + std::to_wstring(rect.bottom) + L"]";
}

void SetAttention(
    WidgetSurfaceHealth& surface,
    std::wstring issueCode,
    std::wstring detail,
    std::wstring recommendedAction) {
    surface.issueCode = std::move(issueCode);
    surface.detail = std::move(detail);
    surface.recommendedAction = std::move(recommendedAction);
}

WidgetSurfaceHealth InspectWidgetSurface(const wallpaper::DesktopWidget& widget, bool compatibilityHealthy) {
    WidgetSurfaceHealth surface;
    surface.widgetId = widget.id;
    surface.monitorId = widget.monitorId.empty() ? L"primary" : widget.monitorId;
    surface.configured = widget.enabled && widget.kind == wallpaper::DesktopWidgetKind::Native;
    if (!surface.configured) {
        SetAttention(surface, L"widget_disabled", L"Widget 未启用 runtime。", L"在小组件页面启用该 Widget 后刷新运行状态。");
        return surface;
    }

    const auto parentTelemetry = wallpaper::InspectDesktopSurfaceParent();
    const HWND expectedParent = parentTelemetry.reported && parentTelemetry.parent && IsWindow(parentTelemetry.parent)
        ? parentTelemetry.parent
        : nullptr;
    const HWND window = FindWidgetSurface(expectedParent, widget.id);
    surface.hwndReady = window && IsWindow(window);
    surface.hwndValue = surface.hwndReady ? reinterpret_cast<std::uintptr_t>(window) : 0;
    wallpaper::DesktopSurfaceZOrderHealth zOrder;

    const auto topology = wallpaper::QueryMonitorTopology();
    surface.monitorReported = topology.Valid();
    const wallpaper::MonitorInfo* monitor = ResolveWidgetMonitor(topology, widget);
    if (surface.monitorReported) {
        surface.monitorValid = monitor != nullptr;
        if (monitor) {
            surface.monitorId = monitor->stableId.empty() ? wallpaper::StableMonitorKey(*monitor) : monitor->stableId;
            const RECT expected = ExpectedWidgetDesktopRect(*monitor, widget);
            surface.expectedLeft = expected.left;
            surface.expectedTop = expected.top;
            surface.expectedRight = expected.right;
            surface.expectedBottom = expected.bottom;
        }
    }

    if (surface.hwndReady) {
        DWORD processId = 0;
        GetWindowThreadProcessId(window, &processId);
        surface.processId = processId;
        surface.processRunning = ProcessRunning(processId);
        surface.parentValid = expectedParent && GetParent(window) == expectedParent;
        const LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
        surface.childStyleValid = (style & WS_CHILD) != 0;
        surface.visible = IsWindowVisible(window) != FALSE;

        if (monitor) {
            RECT actual{};
            surface.geometryReported = GetWindowRect(window, &actual) != FALSE;
            if (surface.geometryReported) {
                const RECT expected{
                    surface.expectedLeft, surface.expectedTop, surface.expectedRight, surface.expectedBottom};
                surface.actualLeft = actual.left;
                surface.actualTop = actual.top;
                surface.actualRight = actual.right;
                surface.actualBottom = actual.bottom;
                surface.geometryValid = RectNear(expected, actual);
            }
        }

        const bool lifecycleTelemetry = HasStructuredLifecycleTelemetry(window);
        if (!lifecycleTelemetry) {
            SetAttention(surface, L"lifecycle_unreported", L"Widget Surface 未报告结构化 lifecycle",
                         L"重启 Widget runtime helper 让 NativeWidgetHost 重建该 Surface。");
            return surface;
        }

        zOrder = wallpaper::InspectDesktopSurfaceZOrder(
            window, wallpaper::DesktopSurfaceTelemetryRole::Widget);
        surface.zOrderReported = zOrder.reported;
        surface.zOrderValid = zOrder.valid;
    }

    const bool lifecycleReady = NativeLifecycleReady(window);
    const bool zOrderReady = surface.zOrderReported && surface.zOrderValid;
    surface.renderingHealthy = surface.SurfaceReady() && lifecycleReady && zOrderReady && compatibilityHealthy;

    if (!expectedParent) {
        SetAttention(surface, L"desktop_parent_unavailable",
                     parentTelemetry.detail.empty() ? L"无法解析当前 Windows 桌面 Surface parent" : parentTelemetry.detail,
                     L"等待 Explorer 桌面层恢复后刷新；Widget runtime 无需依赖 Native WallpaperHost。");
    } else if (!surface.hwndReady) {
        SetAttention(surface, L"surface_missing", L"等待隔离 Surface HWND", L"点击“刷新”；若持续不存在，重启 Widget runtime helper。");
    } else if (!surface.processRunning) {
        SetAttention(surface, L"process_stopped", L"隔离进程未运行", L"重新启用该 Widget；若仍失败，重启 Widget runtime helper。");
    } else if (!surface.parentValid) {
        SetAttention(surface, L"parent_invalid", L"Surface parent 不匹配", L"重启 Explorer 或 Widget runtime helper 以触发 DesktopShellHost 重新挂载。");
    } else if (!surface.childStyleValid) {
        SetAttention(surface, L"child_style_invalid", L"Surface 缺少 WS_CHILD", L"重启 Widget runtime helper；该 Surface 需要由 DesktopShellHost 重新创建。");
    } else if (!surface.visible) {
        SetAttention(surface, L"surface_hidden", L"Surface 当前不可见", L"确认 Widget 已启用并点击“刷新”；若仍隐藏，重启 Widget runtime helper。");
    } else if (!surface.monitorReported) {
        SetAttention(surface, L"monitor_topology_unavailable", L"无法读取当前显示器 topology", L"确认显示器已连接并在 Windows 中启用，然后刷新 Widget 运行状态。");
    } else if (!surface.monitorValid) {
        SetAttention(surface, L"monitor_missing", L"Widget 配置的目标显示器当前不存在：" + surface.monitorId,
                     L"重新连接目标显示器，或在小组件设置中重新选择当前可用显示器。");
    } else if (!surface.geometryReported) {
        SetAttention(surface, L"geometry_unreported", L"无法读取 Widget Surface 实际桌面坐标", L"刷新运行状态；若持续失败，重启 Widget runtime helper。");
    } else if (!surface.geometryValid) {
        const RECT expected{surface.expectedLeft, surface.expectedTop, surface.expectedRight, surface.expectedBottom};
        const RECT actual{surface.actualLeft, surface.actualTop, surface.actualRight, surface.actualBottom};
        SetAttention(surface, L"geometry_mismatch",
                     L"Widget Surface 未恢复到配置位置；expected=" + RectText(expected) + L" actual=" + RectText(actual),
                     L"等待显示器拓扑稳定后刷新；若仍不一致，重启 Explorer 或 Widget runtime helper 以重新应用显示器布局。");
    } else if (!PropertyReady(window, wallpaper::kNativeWidgetPaintReadyProperty)) {
        SetAttention(surface, L"native_paint_pending", L"Native Widget HWND 已创建，但 layered Direct2D 尚未成功呈现。",
                     L"等待一次重绘；若持续未就绪，查看 NativeWidgetHost diagnostics。 ");
    } else if (!surface.zOrderReported) {
        SetAttention(surface, L"zorder_unreported", L"Widget Surface 已绘制；等待 DesktopShell z-order telemetry", L"点击“刷新”；若持续未报告，重启 DesktopShell supervisor。");
    } else if (!surface.zOrderValid) {
        SetAttention(surface, L"zorder_invalid", zOrder.detail.empty() ? L"Widget z-order 无效" : zOrder.detail,
                     L"重启 Explorer 或 DesktopShell supervisor，让 DesktopShellHost 修复 Widget/壁纸/图标层级。");
    } else if (!compatibilityHealthy) {
        SetAttention(surface, L"runtime_diagnostic_unhealthy", L"Widget runtime 兼容诊断报告异常", L"查看 Widget runtime 日志并只重启 Widget helper。");
    } else {
        surface.detail = L"Widget Native layered Direct2D/desktop surface/monitor geometry health ready";
    }
    return surface;
}

} // namespace

WidgetServiceResult WidgetService::CreateNative(
    const NativeWidgetCreateRequest& request,
    wallpaper::DesktopWidget* created) const {
    const auto* definition = wallpaper::NativePresetDefinition(request.preset);
    if (!definition) return {false, L"原生桌面小组件 preset 无效。"};

    wallpaper::DesktopWidgetStore store;
    std::wstring error;
    if (!store.Load(&error)) return LoadFailure(error);
    auto widget = store.CreateManagedNative(
        request.preset,
        request.title,
        request.monitorId,
        request.x,
        request.y,
        definition->defaultWidth,
        definition->defaultHeight,
        &error);
    if (!widget) return {false, error.empty() ? L"创建原生桌面小组件失败。" : error};
    if (created) *created = *widget;
    return {true, L"原生桌面小组件已创建：" + widget->id};
}

WidgetServiceResult WidgetService::Update(const WidgetUpdateRequest& request) const {
    if (request.id.empty()) return {false, L"desktop widget id 不能为空。"};

    wallpaper::DesktopWidgetStore store;
    std::wstring error;
    if (!store.Load(&error)) return LoadFailure(error);
    const auto old = store.Find(request.id);
    if (!old) return {false, L"没有找到桌面小组件：" + request.id};

    if (old->kind == wallpaper::DesktopWidgetKind::Native) {
        wallpaper::NativeWidgetPreset preset{};
        if (!wallpaper::ParseNativePreset(old->source.wstring(), &preset))
            return {false, L"原生桌面小组件 preset 无效：" + old->source.wstring()};
        const auto* definition = wallpaper::NativePresetDefinition(preset);
        if (!definition) return {false, L"原生桌面小组件 preset 定义缺失。"};
        if (request.width && std::fabs(*request.width - definition->defaultWidth) > kPresetGeometryTolerance)
            return {false, L"内置小组件宽度由 preset 拥有，不能通过通用 Update 修改。"};
        if (request.height && std::fabs(*request.height - definition->defaultHeight) > kPresetGeometryTolerance)
            return {false, L"内置小组件高度由 preset 拥有，不能通过通用 Update 修改。"};
    }

    auto widget = *old;
    if (request.title) widget.title = *request.title;
    if (request.monitorId) widget.monitorId = *request.monitorId;
    if (request.x) widget.x = *request.x;
    if (request.y) widget.y = *request.y;
    if (request.width) widget.width = *request.width;
    if (request.height) widget.height = *request.height;
    if (request.enabled) widget.enabled = *request.enabled;

    if (!store.Upsert(widget, &error))
        return {false, error.empty() ? L"更新桌面小组件失败。" : error};
    return {true, L"桌面小组件已更新：" + request.id};
}

WidgetServiceResult WidgetService::Remove(std::wstring_view id) const {
    if (id.empty()) return {false, L"desktop widget id 不能为空。"};
    wallpaper::DesktopWidgetStore store;
    std::wstring error;
    if (!store.Load(&error)) return LoadFailure(error);
    if (!store.Remove(id, &error))
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
    result.enabledCount = static_cast<std::size_t>(std::count_if(
        store.Items().begin(), store.Items().end(), [](const wallpaper::DesktopWidget& widget) {
            return widget.enabled && widget.kind == wallpaper::DesktopWidgetKind::Native;
        }));
    result.detail = ReadWidgetRuntimeDetail();
    result.runtimeReported = !result.detail.empty();
    const bool compatibilityHealthy = result.enabledCount == 0
        ? (result.detail.empty() || result.detail.find(L"未启用桌面小组件") != std::wstring::npos ||
           result.detail.find(L"Widget runtime stopped") != std::wstring::npos)
        : RuntimeDetailLooksHealthy(result.detail);

    result.surfaces.reserve(result.enabledCount);
    for (const auto& widget : store.Items()) {
        if (!widget.enabled) continue;
        if (widget.kind != wallpaper::DesktopWidgetKind::Native) continue;
        result.surfaces.push_back(InspectWidgetSurface(widget, compatibilityHealthy));
    }

    const bool allSurfacesRendering = std::all_of(result.surfaces.begin(), result.surfaces.end(),
        [](const WidgetSurfaceHealth& surface) { return surface.renderingHealthy; });
    result.runtimeHealthy = result.enabledCount == 0
        ? compatibilityHealthy
        : compatibilityHealthy && result.surfaces.size() == result.enabledCount && allSurfacesRendering;
    *health = std::move(result);
    return {true, L"桌面小组件运行状态读取完成。"};
}

} // namespace miaodesk::desktop
