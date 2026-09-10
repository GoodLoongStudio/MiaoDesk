#include "miaodesk/NativeWidgetPainter.h"

#include "miaodesk/MiaoSceneD2DRenderer.h"
#include "miaodesk/MiaoWidgetContentCatalog.h"
#include "miaodesk/RuntimeLogger.h"

#include <d2d1helper.h>
#include <windows.h>

#include <string>

namespace miaodesk::wallpaper {
namespace {

constexpr std::wstring_view kGlassClockDefinitionId = L"com.goodloong.glass-clock";

void LogFallbackOnce(std::wstring_view detail) {
    static std::wstring lastDetail;
    const std::wstring current(detail);
    if (current == lastDetail) return;
    lastDetail = current;
    miaodesk::log::Warn(L"WidgetContent", L"GlassClock .mdwidget 回退 Native painter: " + current);
}

void LogSuccessOnce() {
    static bool logged = false;
    if (logged) return;
    logged = true;
    miaodesk::log::Info(L"WidgetContent", L"GlassClock 已通过 Miao Content Framework / Scene TextRenderer 绘制");
}

} // namespace

bool TryPaintOfficialWidgetContent(
    const NativeWidgetPaintContext& context,
    NativeWidgetPreset preset,
    std::wstring* error) {
    if (error) error->clear();
    if (preset != NativeWidgetPreset::GlassClock || !context.target ||
        context.width <= 1.0f || context.height <= 1.0f) {
        return false;
    }

    const std::wstring source = content::MiaoWidgetContentCatalog::MakeSource(kGlassClockDefinitionId);
    if (source.empty()) {
        if (error) *error = L"GlassClock content source id is invalid.";
        LogFallbackOnce(error ? *error : L"invalid source");
        return false;
    }

    content::ResolvedWidgetContent resolved;
    std::wstring resolveError;
    if (!content::MiaoWidgetContentCatalog::Resolve(source, &resolved, &resolveError)) {
        if (error) *error = resolveError;
        LogFallbackOnce(resolveError);
        return false;
    }
    if (resolved.definition.kind != content::ContentKind::Widget ||
        resolved.definition.runtime != content::ContentRuntimeKind::Scene) {
        const std::wstring detail = L"official GlassClock package is not a Scene widget";
        if (error) *error = detail;
        LogFallbackOnce(detail);
        return false;
    }

    content::MiaoSceneD2DRenderer renderer;
    std::wstring renderError;
    if (!renderer.Load(resolved.packageRoot, context.target, &renderError)) {
        if (error) *error = renderError;
        LogFallbackOnce(renderError);
        return false;
    }

    // NativeWidgetHost owns BeginDraw/EndDraw. Clear only the current frame and
    // let the Scene renderer paint inside that transaction. If Scene rendering
    // fails, the caller immediately invokes the legacy painter, which clears
    // again and preserves the previous production behavior.
    if (context.clearBackground) {
        context.target->Clear(context.opaqueSurface
            ? D2D1::ColorF(0.035f, 0.10f, 0.22f, 1.0f)
            : D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
    }

    const float timeSeconds = static_cast<float>(GetTickCount64() / 1000.0);
    if (!renderer.Draw(timeSeconds, D2D1::SizeF(context.width, context.height), &renderError)) {
        if (error) *error = renderError;
        LogFallbackOnce(renderError);
        return false;
    }

    LogSuccessOnce();
    return true;
}

} // namespace miaodesk::wallpaper
