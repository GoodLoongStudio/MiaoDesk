#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "miaodesk/DesktopWidgetStore.h"
#include "miaodesk/MiaoWidgetContentCatalog.h"
#include "miaodesk/NativeWidgetPreset.h"

namespace miaodesk::desktop {

struct WidgetServiceResult {
    bool success{};
    std::wstring message;
};

struct NativeWidgetCreateRequest {
    wallpaper::NativeWidgetPreset preset{wallpaper::NativeWidgetPreset::GlassClock};
    std::wstring title;
    std::wstring monitorId;
    float x{0.68f};
    float y{0.05f};
    float width{0.28f};
    float height{0.18f};
};

struct ContentWidgetCreateRequest {
    std::wstring definitionId;
    std::wstring title;
    std::wstring monitorId;
    float x{0.68f};
    float y{0.05f};
    // Omitted dimensions come from the package geometry contract. Content
    // callers should not duplicate a package's default size/aspect ratio.
    std::optional<float> width;
    std::optional<float> height;
    // Content stays opt-in while host routing is staged. The first supported
    // enabled route is the official GlassClock Scene package.
    bool enabled{false};
};

struct WidgetUpdateRequest {
    std::wstring id;
    std::optional<std::wstring> title;
    std::optional<std::wstring> monitorId;
    std::optional<float> x;
    std::optional<float> y;
    std::optional<float> width;
    std::optional<float> height;
    std::optional<bool> enabled;
};

// One configured Widget matched to its live desktop surface. Runtime inspection
// remains owned by the Widget domain. UI/Pi receive both machine-readable
// issueCode and human-readable recommendedAction so callers never need to infer
// remediation from HWND/Native rendering implementation details.
struct WidgetSurfaceHealth {
    std::wstring widgetId;
    std::wstring monitorId;
    std::uint32_t processId{};
    std::uintptr_t hwndValue{};
    bool configured{};
    bool processRunning{};
    bool hwndReady{};
    bool parentValid{};
    bool childStyleValid{};
    bool visible{};
    bool monitorReported{};
    bool monitorValid{};
    bool geometryReported{};
    bool geometryValid{};
    int expectedLeft{};
    int expectedTop{};
    int expectedRight{};
    int expectedBottom{};
    int actualLeft{};
    int actualTop{};
    int actualRight{};
    int actualBottom{};
    bool zOrderValid{};
    bool zOrderReported{};
    bool renderingHealthy{};
    std::wstring issueCode;
    std::wstring recommendedAction;
    std::wstring detail;

    bool SurfaceReady() const noexcept {
        return configured && processRunning && hwndReady && parentValid && childStyleValid && visible &&
               monitorReported && monitorValid && geometryReported && geometryValid;
    }
};

// Caller-facing Widget runtime summary. UI/Pi clients consume this through
// DesktopSnapshot rather than reading wallpaper.ini or enumerating HWNDs.
struct WidgetRuntimeHealth {
    std::size_t configuredCount{};
    std::size_t enabledCount{};
    bool runtimeReported{};
    bool runtimeHealthy{};
    std::vector<WidgetSurfaceHealth> surfaces;
    std::wstring detail;

    bool Healthy() const noexcept {
        return enabledCount == 0 || (runtimeReported && runtimeHealthy);
    }
};

// Widget domain service. DesktopWidgetStore is an implementation detail behind
// this boundary rather than a public UI/AI product API.
class WidgetService {
public:
    WidgetServiceResult CreateNative(
        const NativeWidgetCreateRequest& request,
        wallpaper::DesktopWidget* created = nullptr) const;

    // Persists a validated stable content:<definitionId> source. Package-owned
    // geometry is applied by default so persisted instances cannot silently
    // drift away from the .mdwidget contract.
    WidgetServiceResult CreateContent(
        const ContentWidgetCreateRequest& request,
        wallpaper::DesktopWidget* created = nullptr) const {
        const auto source = content::MiaoWidgetContentCatalog::MakeSource(request.definitionId);
        if (source.empty()) return {false, L"Content widget definition id 无效。"};

        content::ResolvedWidgetContent resolved;
        std::wstring error;
        if (!content::MiaoWidgetContentCatalog::Resolve(source, &resolved, &error)) {
            return {false, error.empty() ? L"找不到或无法验证 Content widget package。" : error};
        }
        if (resolved.definition.kind != content::ContentKind::Widget) {
            return {false, L"Content definition 不是 widget：" + resolved.definition.id};
        }
        if (request.enabled && source != L"content:com.goodloong.glass-clock") {
            return {false, L"该 Content widget 尚未接入桌面运行时；当前仅 GlassClock 支持启用。"};
        }

        wallpaper::DesktopWidget widget;
        widget.id = wallpaper::DesktopWidgetStore::MakeId();
        widget.kind = wallpaper::DesktopWidgetKind::Content;
        widget.title = request.title.empty() ? resolved.definition.name : request.title;
        widget.source = source;
        widget.monitorId = request.monitorId;
        widget.x = request.x;
        widget.y = request.y;
        widget.width = request.width.value_or(resolved.definition.geometry.defaultWidth);
        widget.height = request.height.value_or(resolved.definition.geometry.defaultHeight);
        widget.enabled = request.enabled;

        content::ContentInstance instance;
        instance.instanceId = widget.id;
        instance.definitionId = resolved.definition.id;
        instance.monitorId = widget.monitorId;
        instance.enabled = widget.enabled;
        instance.x = widget.x;
        instance.y = widget.y;
        instance.width = widget.width;
        instance.height = widget.height;
        if (!content::MiaoContentModel::ValidateInstance(resolved.definition, instance, &error)) {
            return {false, error.empty() ? L"Content widget geometry/instance 无效。" : error};
        }

        wallpaper::DesktopWidgetStore store;
        if (!store.Load(&error))
            return {false, error.empty() ? L"无法读取桌面小组件状态。" : error};
        const auto saved = store.Upsert(std::move(widget), &error);
        if (!saved)
            return {false, error.empty() ? L"创建 Content 桌面小组件失败。" : error};
        if (created) *created = *saved;
        return {true, L"Content 桌面小组件已创建：" + saved->id};
    }

    WidgetServiceResult Update(const WidgetUpdateRequest& request) const;
    WidgetServiceResult Remove(std::wstring_view id) const;
    WidgetServiceResult List(std::vector<wallpaper::DesktopWidget>* widgets) const;
    WidgetServiceResult Find(std::wstring_view id, wallpaper::DesktopWidget* widget) const;
    WidgetServiceResult GetRuntimeHealth(WidgetRuntimeHealth* health) const;
};

} // namespace miaodesk::desktop