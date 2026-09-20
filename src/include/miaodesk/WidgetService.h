#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "miaodesk/ContentWidgetInstanceStore.h"
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
    std::optional<float> width;
    std::optional<float> height;
    bool enabled{true};
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

struct ContentWidgetSettingsSnapshot {
    std::wstring widgetId;
    std::wstring title;
    content::ContentDefinition definition;
    content::ContentParameterValues values;
};

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

class WidgetService {
public:
    WidgetServiceResult CreateNative(
        const NativeWidgetCreateRequest& request,
        wallpaper::DesktopWidget* created = nullptr) const;

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
        if (request.enabled && resolved.definition.runtime != content::ContentRuntimeKind::Scene) {
            return {false, L"该 Content widget 的 runtime 尚未接入桌面宿主；当前支持 Scene runtime。"};
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

    WidgetServiceResult GetContentSettings(
        std::wstring_view id,
        ContentWidgetSettingsSnapshot* settings) const {
        if (!settings) return {false, L"Content widget settings 输出不能为空。"};
        wallpaper::DesktopWidgetStore store;
        std::wstring error;
        if (!store.Load(&error)) return {false, error.empty() ? L"无法读取桌面小组件状态。" : error};
        const auto widget = store.Find(id);
        if (!widget) return {false, L"没有找到桌面小组件：" + std::wstring(id)};
        if (widget->kind != wallpaper::DesktopWidgetKind::Content)
            return {false, L"该桌面小组件不是 Content 类型。"};

        content::ResolvedWidgetContent resolved;
        if (!content::MiaoWidgetContentCatalog::Resolve(widget->source.wstring(), &resolved, &error))
            return {false, error.empty() ? L"无法解析 Content widget package。" : error};
        content::ContentParameterValues overrides;
        if (!content::ContentWidgetInstanceStore::LoadOverrides(widget->id, resolved.definition, &overrides, &error))
            return {false, error.empty() ? L"无法读取 Content widget 参数。" : error};
        content::ContentParameterValues values;
        if (!content::MiaoContentModel::ResolveParameterValues(resolved.definition, overrides, &values, &error))
            return {false, error.empty() ? L"Content widget 参数无效。" : error};

        settings->widgetId = widget->id;
        settings->title = widget->title;
        settings->definition = std::move(resolved.definition);
        settings->values = std::move(values);
        return {true, L"Content widget settings 读取完成。"};
    }

    WidgetServiceResult GetContentParameters(
        std::wstring_view id,
        content::ContentParameterValues* values) const {
        if (!values) return {false, L"Content parameter 输出不能为空。"};
        ContentWidgetSettingsSnapshot settings;
        const auto result = GetContentSettings(id, &settings);
        if (!result.success) return result;
        *values = std::move(settings.values);
        return {true, L"Content widget 参数读取完成。"};
    }

    // Applies a set of changed fields in one atomic instance-file replacement.
    // Existing overrides not present in `changes` are preserved. Callers should
    // send only user-modified fields so untouched package defaults remain live
    // across future .mdwidget upgrades.
    WidgetServiceResult SetContentParameters(
        std::wstring_view id,
        const content::ContentParameterValues& changes) const {
        if (changes.empty()) return {true, L"Content widget 参数没有变化。"};
        wallpaper::DesktopWidgetStore store;
        std::wstring error;
        if (!store.Load(&error)) return {false, error.empty() ? L"无法读取桌面小组件状态。" : error};
        const auto widget = store.Find(id);
        if (!widget) return {false, L"没有找到桌面小组件：" + std::wstring(id)};
        if (widget->kind != wallpaper::DesktopWidgetKind::Content)
            return {false, L"该桌面小组件不是 Content 类型。"};

        content::ResolvedWidgetContent resolved;
        if (!content::MiaoWidgetContentCatalog::Resolve(widget->source.wstring(), &resolved, &error))
            return {false, error.empty() ? L"无法解析 Content widget package。" : error};
        content::ContentParameterValues overrides;
        if (!content::ContentWidgetInstanceStore::LoadOverrides(widget->id, resolved.definition, &overrides, &error))
            return {false, error.empty() ? L"无法读取 Content widget 参数。" : error};
        for (const auto& [key, value] : changes) {
            if (!content::MiaoContentModel::FindParameter(resolved.definition, key))
                return {false, L"未知 Content widget 参数：" + key};
            overrides[key] = value;
        }
        content::ContentParameterValues validated;
        if (!content::MiaoContentModel::ResolveParameterValues(resolved.definition, overrides, &validated, &error))
            return {false, error.empty() ? L"Content widget 参数无效。" : error};
        if (!content::ContentWidgetInstanceStore::SaveOverrides(widget->id, resolved.definition, overrides, &error))
            return {false, error.empty() ? L"Content widget 参数保存失败。" : error};
        return {true, L"Content widget 参数已原子更新。"};
    }

    WidgetServiceResult SetContentParameter(
        std::wstring_view id,
        std::wstring_view key,
        content::ContentParameterValue value) const {
        content::ContentParameterValues changes;
        changes.emplace(std::wstring(key), std::move(value));
        return SetContentParameters(id, changes);
    }

    WidgetServiceResult ResetContentParameters(std::wstring_view id) const {
        wallpaper::DesktopWidgetStore store;
        std::wstring error;
        if (!store.Load(&error)) return {false, error.empty() ? L"无法读取桌面小组件状态。" : error};
        const auto widget = store.Find(id);
        if (!widget) return {false, L"没有找到桌面小组件：" + std::wstring(id)};
        if (widget->kind != wallpaper::DesktopWidgetKind::Content)
            return {false, L"该桌面小组件不是 Content 类型。"};
        if (!content::ContentWidgetInstanceStore::Remove(widget->id, &error))
            return {false, error.empty() ? L"Content widget 参数重置失败。" : error};
        return {true, L"Content widget 参数已恢复 package 默认值。"};
    }

    WidgetServiceResult Update(const WidgetUpdateRequest& request) const;
    WidgetServiceResult Remove(std::wstring_view id) const;
    WidgetServiceResult List(std::vector<wallpaper::DesktopWidget>* widgets) const;
    WidgetServiceResult Find(std::wstring_view id, wallpaper::DesktopWidget* widget) const;
    WidgetServiceResult GetRuntimeHealth(WidgetRuntimeHealth* health) const;
};

} // namespace miaodesk::desktop
