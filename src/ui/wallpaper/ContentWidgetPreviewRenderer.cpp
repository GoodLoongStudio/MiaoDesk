#include "miaodesk/ContentWidgetPreviewRenderer.h"

#include "miaodesk/MiaoSceneD2DRenderer.h"
#include "miaodesk/MiaoWidgetContentCatalog.h"
#include "miaodesk/NativeWeatherService.h"

#include <d2d1.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>
#include <utility>

using Microsoft::WRL::ComPtr;

namespace miaodesk::wallpaper {
namespace {

bool Fail(std::wstring* error, std::wstring message) {
    if (error) *error = std::move(message);
    return false;
}

bool HasCapability(const content::ContentDefinition& definition, std::wstring_view capability) {
    if (std::find(definition.capabilities.begin(), definition.capabilities.end(), capability) !=
        definition.capabilities.end()) return true;
    if (capability == L"weather.read") {
        return std::find(definition.capabilities.begin(), definition.capabilities.end(), L"weather") !=
               definition.capabilities.end();
    }
    return false;
}

bool PublishWeather(
    content::MiaoSceneD2DRenderer& renderer,
    const content::ContentDefinition& definition,
    std::wstring* error) {
    renderer.ClearDataValues();
    if (!HasCapability(definition, L"weather.read")) return true;

    NativeWeatherSnapshot weather;
    const bool available = NativeWeatherService::ReadCachedSnapshot(&weather) && weather.valid;
    auto publish = [&](std::wstring_view path, content::PropertyValue value) {
        return renderer.SetDataValue(path, std::move(value), error);
    };

    if (!publish(L"weather.available", available) ||
        !publish(L"weather.location", available ? weather.location : std::wstring{}) ||
        !publish(L"weather.temperatureC", static_cast<std::int64_t>(available ? weather.temperatureC : 0)) ||
        !publish(L"weather.highC", static_cast<std::int64_t>(available ? weather.highC : 0)) ||
        !publish(L"weather.lowC", static_cast<std::int64_t>(available ? weather.lowC : 0)) ||
        !publish(L"weather.code", static_cast<std::int64_t>(available ? weather.weatherCode : 0)) ||
        !publish(L"weather.condition", available ? weather.condition : std::wstring{}) ||
        !publish(L"weather.observedTime", available ? weather.observedTime : std::wstring{}) ||
        !publish(L"weather.status", available ? weather.status : std::wstring(L"天气数据暂不可用"))) return false;

    for (std::size_t i = 0; i < weather.hours.size(); ++i) {
        const auto& hour = weather.hours[i];
        const std::wstring prefix = L"weather.hour" + std::to_wstring(i) + L".";
        if (!publish(prefix + L"label", available ? hour.label : std::wstring{}) ||
            !publish(prefix + L"temperatureC", static_cast<std::int64_t>(available ? hour.temperatureC : 0)) ||
            !publish(prefix + L"code", static_cast<std::int64_t>(available ? hour.weatherCode : 0))) return false;
    }
    return true;
}

RECT FitWidgetAspect(const RECT& bounds, const DesktopWidget& widget) {
    RECT render = bounds;
    InflateRect(&render, -6, -6);
    if (render.right <= render.left || render.bottom <= render.top) render = bounds;

    const int availableW = std::max<LONG>(1, render.right - render.left);
    const int availableH = std::max<LONG>(1, render.bottom - render.top);
    const float screenW = static_cast<float>(std::max(1, GetSystemMetrics(SM_CXSCREEN)));
    const float screenH = static_cast<float>(std::max(1, GetSystemMetrics(SM_CYSCREEN)));
    const float physicalW = std::max(1.0f, widget.width * screenW);
    const float physicalH = std::max(1.0f, widget.height * screenH);
    const float aspect = std::clamp(physicalW / physicalH, 0.25f, 5.0f);

    if (static_cast<float>(availableW) / static_cast<float>(availableH) > aspect) {
        const int fittedW = std::max(1, static_cast<int>(std::lround(availableH * aspect)));
        render.left += (availableW - fittedW) / 2;
        render.right = render.left + fittedW;
    } else {
        const int fittedH = std::max(1, static_cast<int>(std::lround(availableW / aspect)));
        render.top += (availableH - fittedH) / 2;
        render.bottom = render.top + fittedH;
    }
    return render;
}

} // namespace

struct ContentWidgetPreviewRenderer::Impl {
    ComPtr<ID2D1Factory> factory;
    ComPtr<ID2D1DCRenderTarget> target;

    bool EnsureTarget(std::wstring* error) {
        if (!factory) {
            if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf())))
                return Fail(error, L"无法创建 Content preview Direct2D factory。");
        }
        if (!target) {
            const auto properties = D2D1::RenderTargetProperties(
                D2D1_RENDER_TARGET_TYPE_DEFAULT,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
                96.0f, 96.0f);
            if (FAILED(factory->CreateDCRenderTarget(&properties, target.GetAddressOf())))
                return Fail(error, L"无法创建 Content preview DCRenderTarget。");
        }
        return true;
    }

    bool Draw(
        HDC dc,
        const RECT& bounds,
        const DesktopWidget& widget,
        desktop::DesktopWidgetController& controller,
        std::wstring* error) {
        if (error) error->clear();
        if (!dc || bounds.right <= bounds.left || bounds.bottom <= bounds.top)
            return Fail(error, L"Content preview 绘制区域无效。");
        if (widget.kind != DesktopWidgetKind::Content)
            return Fail(error, L"Content preview 只接受 Content widget。");
        if (!EnsureTarget(error)) return false;

        content::ResolvedWidgetContent resolved;
        if (!content::MiaoWidgetContentCatalog::Resolve(widget.source.wstring(), &resolved, error)) return false;
        if (resolved.definition.kind != content::ContentKind::Widget ||
            resolved.definition.runtime != content::ContentRuntimeKind::Scene)
            return Fail(error, L"Content preview 当前只支持 Scene widget。");

        desktop::ContentWidgetSettingsSnapshot settings;
        const auto settingsResult = controller.GetContentSettings(widget.id, &settings);
        if (!settingsResult.success) return Fail(error, settingsResult.message);

        const RECT render = FitWidgetAspect(bounds, widget);
        if (FAILED(target->BindDC(dc, &render)))
            return Fail(error, L"Content preview 无法绑定目标 HDC。");
        target->SetDpi(96.0f, 96.0f);

        content::MiaoSceneD2DRenderer renderer;
        if (!renderer.Load(resolved.packageRoot, target.Get(), error)) return false;
        for (const auto& parameter : settings.definition.parameters) {
            const auto found = settings.values.find(parameter.key);
            if (found == settings.values.end()) continue;
            if (!renderer.SetParameter(parameter.runtimeId, found->second, error)) return false;
        }
        if (!PublishWeather(renderer, settings.definition, error)) return false;

        target->BeginDraw();
        target->Clear(D2D1::ColorF(0.973f, 0.980f, 0.992f, 1.0f));
        const bool rendered = renderer.Draw(
            static_cast<float>(GetTickCount64() / 1000.0), target->GetSize(), error);
        const HRESULT end = target->EndDraw();
        renderer.Reset();
        if (end == D2DERR_RECREATE_TARGET) {
            target.Reset();
            return Fail(error, L"Content preview Direct2D target 需要重建。");
        }
        if (FAILED(end)) return Fail(error, L"Content preview Direct2D 绘制失败。");
        return rendered;
    }

    void Reset() noexcept {
        target.Reset();
        factory.Reset();
    }
};

ContentWidgetPreviewRenderer::ContentWidgetPreviewRenderer() : impl_(std::make_unique<Impl>()) {}
ContentWidgetPreviewRenderer::~ContentWidgetPreviewRenderer() = default;

bool ContentWidgetPreviewRenderer::Draw(
    HDC dc,
    const RECT& bounds,
    const DesktopWidget& widget,
    desktop::DesktopWidgetController& controller,
    std::wstring* error) {
    return impl_->Draw(dc, bounds, widget, controller, error);
}

void ContentWidgetPreviewRenderer::Reset() noexcept { impl_->Reset(); }

} // namespace miaodesk::wallpaper
