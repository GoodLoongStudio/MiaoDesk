#include "miaodesk/MiaoWidgetGeometry.h"

#include <algorithm>
#include <cwchar>

namespace miaodesk::desktop {
namespace {

float SafeAspect(float width, float height) noexcept {
    if (!(width > 0.0f) || !(height > 0.0f)) return 0.0f;
    const float value = width / height;
    return std::isfinite(value) ? value : 0.0f;
}

float ClampFinite(float value, float low, float high) noexcept {
    if (!std::isfinite(value)) return low;
    return std::clamp(value, low, high);
}

} // namespace

WidgetPixelBox ResolveWidgetPixelBox(
    const content::ContentGeometryPolicy& geometry,
    const content::ContentInstance& instance,
    const MonitorPixels& monitor) noexcept {
    WidgetPixelBox box;
    // 屏幕尺寸不信任:0 或 NaN 都让这个函数变成"什么也答不上来",而不是返回一个
    // 看似合理、其实会让宿主建不出渲染目标的盒子。
    if (!(monitor.width > 0.0f) || !(monitor.height > 0.0f) || !std::isfinite(monitor.width) ||
        !std::isfinite(monitor.height))
        return box;

    // 用组件自己声明的那一档:固定尺寸时 default 就是唯一允许的尺寸(resizeAllowed
    // 为假时 instance 必须等于 default,否则 ValidateInstance 已经拒了)。
    float width = instance.width;
    float height = instance.height;
    if (!geometry.resizeAllowed) {
        width = geometry.defaultWidth;
        height = geometry.defaultHeight;
    }
    width = ClampFinite(width, 0.0f, 1.0f);
    height = ClampFinite(height, 0.0f, 1.0f);

    const float pixelWidth = width * monitor.width;
    const float pixelHeight = height * monitor.height;
    box.x = ClampFinite(instance.x, 0.0f, 1.0f) * monitor.width;
    box.y = ClampFinite(instance.y, 0.0f, 1.0f) * monitor.height;
    box.width = pixelWidth;
    box.height = pixelHeight;
    // 夹回屏内:优先留住宽/高本身,位置退让。宿主拿这个数去建渲染目标,
    // 一个越界的盒子会让 UpdateLayeredWindow 直接失败。
    if (box.x + box.width > monitor.width) box.x = std::max(0.0f, monitor.width - box.width);
    if (box.y + box.height > monitor.height) box.y = std::max(0.0f, monitor.height - box.height);
    box.aspect = SafeAspect(box.width, box.height);
    box.screenFraction = (pixelWidth * pixelHeight) / (monitor.width * monitor.height);
    return box;
}

WidgetAspectVerdict JudgeWidgetAspect(
    const content::ContentGeometryPolicy& geometry,
    const content::ContentInstance& instance,
    const MonitorPixels& monitor) noexcept {
    WidgetAspectVerdict verdict;
    if (!geometry.aspectRatio.has_value()) {
        // 没声明就说没声明。下面三个数一旦读出来就会被当成"组件是方的",
        // 而 manifest 一个字都没承诺过。
        verdict.declared = false;
        verdict.declaredAspect = 0.0f;
        verdict.pixelAspect = 0.0f;
        verdict.distortion = 1.0f;
        verdict.exact = true;
        return verdict;
    }
    verdict.declared = true;
    verdict.declaredAspect = *geometry.aspectRatio;
    const auto box = ResolveWidgetPixelBox(geometry, instance, monitor);
    verdict.pixelAspect = box.aspect;
    if (verdict.declaredAspect > 0.0f && verdict.pixelAspect > 0.0f) {
        verdict.distortion = verdict.pixelAspect / verdict.declaredAspect;
        verdict.exact = std::fabs(verdict.pixelAspect - verdict.declaredAspect) <=
                        kWidgetAspectTolerance * std::max(1.0f, verdict.declaredAspect);
    } else {
        verdict.distortion = 1.0f;
        verdict.exact = false;
    }
    return verdict;
}

WidgetAspectVerdict JudgeWidgetAspectOfBox(const WidgetPixelBox& box,
                                           const std::optional<float>& declaredAspect) noexcept {
    WidgetAspectVerdict verdict;
    if (!declaredAspect.has_value()) {
        verdict.declared = false;
        verdict.declaredAspect = 0.0f;
        verdict.pixelAspect = 0.0f;
        verdict.distortion = 1.0f;
        verdict.exact = true;
        return verdict;
    }
    verdict.declared = true;
    verdict.declaredAspect = *declaredAspect;
    // 比例从 width/height 现算,不读 box.aspect。
    //
    // box.aspect 是 ResolveWidgetPixelBox 顺手填的展示字段;宿主拿到盒子之后可能改它
    // (DPI、边界夹紧、信箱化),而它没有义务记得同步那个字段。第一版就是读 box.aspect,
    // 于是"把 height 调小 24 像素"这种改动拿到的还是旧比例的裁决 —— 而这一条恰好是
    // 诊断要报的信息。WidgetGeometryTest 里那条"盒子与推算不同时裁决跟着变"逮到的。
    const float width = box.width;
    const float height = box.height;
    verdict.pixelAspect = (width > 0.0f && height > 0.0f) ? width / height : 0.0f;
    if (verdict.declaredAspect > 0.0f && verdict.pixelAspect > 0.0f) {
        verdict.distortion = verdict.pixelAspect / verdict.declaredAspect;
        verdict.exact = std::fabs(verdict.pixelAspect - verdict.declaredAspect) <=
                        kWidgetAspectTolerance * std::max(1.0f, verdict.declaredAspect);
    } else {
        verdict.distortion = 1.0f;
        verdict.exact = false;
    }
    return verdict;
}

WidgetPixelBox FitAspectInside(const WidgetPixelBox& box, float aspect) noexcept {
    WidgetPixelBox fitted = box;
    if (!(aspect > 0.0f) || !std::isfinite(aspect)) return fitted;
    if (!(box.width > 0.0f) || !(box.height > 0.0f)) return fitted;

    const float widthForAspect = box.height * aspect;
    const float heightForAspect = box.width / aspect;
    // 取更紧的那一个方向:内盒必须完全在盒子里,不许越出,也不许裁掉内容。
    if (widthForAspect <= box.width) {
        fitted.width = widthForAspect;
        fitted.height = box.height;
    } else {
        fitted.width = box.width;
        fitted.height = heightForAspect;
    }
    fitted.x = box.x + (box.width - fitted.width) * 0.5f;
    fitted.y = box.y + (box.height - fitted.height) * 0.5f;
    fitted.aspect = SafeAspect(fitted.width, fitted.height);
    fitted.screenFraction = box.screenFraction;
    return fitted;
}

WidgetLayout NormalizeWidgetLayout(const WidgetLayout& layout) noexcept {
    WidgetLayout out;
    // 非有限输入落到默认值,而不是原样放行。std::clamp 对 NaN 是恒等函数,
    // 所以"先夹紧"挡不住它,必须先换掉。inf 同理(abs(inf) 夹紧后是本该有的上限,
    // 但 -inf 会夹成 0、让组件贴到左上角,那也是一个说不通的位置)。
    auto finiteOr = [](float value, float fallback) {
        return std::isfinite(value) ? value : fallback;
    };
    out.x = finiteOr(layout.x, kWidgetLayoutDefaultX);
    out.y = finiteOr(layout.y, kWidgetLayoutDefaultY);
    out.width = finiteOr(layout.width, kWidgetLayoutDefaultWidth);
    out.height = finiteOr(layout.height, kWidgetLayoutDefaultHeight);

    out.x = std::clamp(out.x, 0.0f, kWidgetLayoutMaxPosition);
    out.y = std::clamp(out.y, 0.0f, kWidgetLayoutMaxPosition);
    out.width = std::clamp(out.width, kWidgetLayoutMinSize, 1.0f);
    out.height = std::clamp(out.height, kWidgetLayoutMinSize, 1.0f);
    if (out.x + out.width > 1.0f) out.width = 1.0f - out.x;
    if (out.y + out.height > 1.0f) out.height = 1.0f - out.y;
    // 这里**没有**"最后再把 width/height 夹一次下限"。
    // 它是不可达的:位置已被夹到 <= 0.95,于是 1.0f - x >= 0.05000001 > 0.05,
    // 收边之后本来就不会跌破下限。变异检测证实了这一点 —— 把那行删掉,104 项断言
    // 全绿。一行挡不住任何事的代码只会让人以为这里曾经出过 0 宽的组件;
    // 而"收边之后仍 >= 下限"由上面那条随机采样 2000 个样本的循环钉住。
    return out;
}

std::wstring FormatAspect(float value) noexcept {
    if (!std::isfinite(value)) return L"nan";
    wchar_t buffer[32];
    swprintf(buffer, sizeof(buffer) / sizeof(buffer[0]), L"%.3f", static_cast<double>(value));
    return buffer;
}

} // namespace miaodesk::desktop
