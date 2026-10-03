// WPRO-01 组件几何:归一化数在真实屏幕上到底是多大。
//
// 值得单独测的理由:geometry 全用归一化写,于是"这个组件有多大"在写 manifest 时是
// 答不上来的;而产品此前没有任何地方把它算出来过。本模块把它算出来,于是
// "manifest 承诺正方形的组件,在 16:9 屏幕上其实是 1.78:1"这件事在本机就能看见。
//
// 这里验的是算术,不验渲染:真正把它落到像素要改宿主,那需要 Windows。
#include "miaodesk/MiaoWidgetGeometry.h"
#include "miaodesk/MiaoContentModel.h"

#include <cmath>
#include <cstdio>
#include <cstddef>
#include <limits>
#include <optional>
#include <string>

namespace miaodesk::desktop {
namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("FAIL  %s\n", what.c_str());
    }
}

void CheckNear(float actual, float expected, float tolerance, const std::string& what) {
    ++g_checks;
    if (!(std::fabs(actual - expected) <= tolerance)) {
        ++g_failures;
        std::printf("FAIL  %s (期望 %.6f,实际 %.6f)\n", what.c_str(), expected, actual);
    }
}

using namespace miaodesk::content;

// 随产品发行的那三个官方组件的真实形状(逐个从 manifest.json 抄来)。
ContentDefinition ShippedLike(const char* id, float w, float h, bool resize,
                              const std::optional<float>* aspect) {
    ContentDefinition d;
    for (const char* p = id; *p; ++p) d.id.push_back(static_cast<wchar_t>(*p));
    d.kind = ContentKind::Widget;
    d.runtime = ContentRuntimeKind::Scene;
    d.entry = L"scene.json";
    d.geometry.defaultWidth = w;
    d.geometry.defaultHeight = h;
    d.geometry.resizeAllowed = resize;
    if (!resize) {
        d.geometry.minWidth = w;
        d.geometry.minHeight = h;
        d.geometry.maxWidth = w;
        d.geometry.maxHeight = h;
    }
    if (aspect) d.geometry.aspectRatio = *aspect;
    return d;
}

ContentInstance At(float x, float y, float w, float h) {
    ContentInstance instance;
    instance.instanceId = L"instance://probe";
    instance.definitionId = L"builtin/probe";
    instance.x = x;
    instance.y = y;
    instance.width = w;
    instance.height = h;
    return instance;
}

} // namespace
} // namespace miaodesk

int wmain() {
    using namespace miaodesk;
    using namespace miaodesk::desktop;

    const MonitorPixels fhd{1920.0f, 1080.0f};

    // ---- 1. 归一化 → 像素,就是用户看见的那个数 ----
    {
        const std::optional<float> square = 1.0f;
        auto clock = ::miaodesk::desktop::ShippedLike("glass-clock", 0.30f, 0.30f, false, &square);
        const auto box = ResolveWidgetPixelBox(clock.geometry, At(0.35f, 0.35f, 0.30f, 0.30f), fhd);
        CheckNear(box.width, 576.0f, 0.001f, "0.30 宽在 1920 上是 576 像素");
        CheckNear(box.height, 324.0f, 0.001f, "0.30 高在 1080 上是 324 像素");
        CheckNear(box.x, 0.35f * 1920.0f, 0.001f, "x 按屏幕宽换算");
        CheckNear(box.y, 0.35f * 1080.0f, 0.001f, "y 按屏幕高换算");
        CheckNear(box.screenFraction, 576.0f * 324.0f / (1920.0f * 1080.0f), 1e-6f,
                  "占屏比例与分辨率无关(0.09)");
        CheckNear(box.screenFraction, 0.09f, 1e-6f, "占屏比例就是 0.09");

        // 同一份 manifest 在 4K 上:占屏还是 9%,像素变成 4 倍。
        const auto uhd = ResolveWidgetPixelBox(clock.geometry, At(0.35f, 0.35f, 0.30f, 0.30f),
                                               MonitorPixels{3840.0f, 2160.0f});
        CheckNear(uhd.width, 1152.0f, 0.01f, "同一个归一化值在 4K 上是 1152 像素");
        CheckNear(uhd.screenFraction, box.screenFraction, 1e-6f, "占屏比例不随分辨率变");
    }

    // ---- 2. 核心缺口:声明的宽高比在 16:9 上不成立 ----
    {
        const std::optional<float> square = 1.0f;
        auto clock = ::miaodesk::desktop::ShippedLike("glass-clock", 0.30f, 0.30f, false, &square);
        for (const auto& monitor : kReferenceMonitors) {
            const auto verdict =
                JudgeWidgetAspect(clock.geometry, At(0.35f, 0.35f, 0.30f, 0.30f), monitor.pixels);
            (void)verdict;
        }
        const auto verdict = JudgeWidgetAspect(clock.geometry, At(0.35f, 0.35f, 0.30f, 0.30f), fhd);
        Check(verdict.declared, "声明了 aspectRatio 就被看见");
        CheckNear(verdict.declaredAspect, 1.0f, 0.0f, "声明的比例是 1.0");
        CheckNear(verdict.pixelAspect, 576.0f / 324.0f, 1e-5f, "FHD 上实际比例是 1.777…");
        Check(!verdict.exact, "FHD 上与声明的 1.0 不相等");
        CheckNear(verdict.distortion, (576.0f / 324.0f), 1e-5f, "失真是 1.78 倍(被拉宽)");

        // 只有在屏幕本身接近 1:1 时才成立。1600×1200 → 480×360,比例 1.333,仍然不成立。
        const auto fourByThree = JudgeWidgetAspect(clock.geometry, At(0.0f, 0.0f, 0.30f, 0.30f),
                                                   MonitorPixels{1600.0f, 1200.0f});
        Check(!fourByThree.exact, "4:3 屏幕上也不成立(1.33 对 1.00)");

        // 而要让声明恰好成立,归一化盒必须按屏幕比例反过来配。这是本模块最该钉住的
        // 一条等式:像素宽高比 = (归一化宽 / 归一化高) × (屏幕宽 / 屏幕高)。
        // 于是"声明 2.389、屏幕 2.389"要配的是 w = h —— 一个 1:1 的归一化盒,
        // 而不是一个 21:9 形状的归一化盒。作者以为自己在写形状,其实写的是
        // "相对于屏幕的比例",而形状是它乘上屏幕比例的结果。
        const std::optional<float> ultrawide = 3440.0f / 1440.0f;
        auto banner = ::miaodesk::desktop::ShippedLike("banner", 0.30f, 0.30f, false, &ultrawide);
        const auto uw = JudgeWidgetAspect(banner.geometry, At(0.0f, 0.0f, 0.30f, 0.30f),
                                          MonitorPixels{3440.0f, 1440.0f});
        Check(uw.exact, "声明 2.389 配 21:9 屏幕时要 w = h,不是 w/h = 2.389");
        CheckNear(uw.distortion, 1.0f, 1e-3f, "一致时失真为 1");

        // 等式本身,在三种屏幕上逐个验。
        for (const auto& monitor : kReferenceMonitors) {
            auto kit = ::miaodesk::desktop::ShippedLike("kit", 0.25f, 0.40f, true, nullptr);
            const auto box = ResolveWidgetPixelBox(kit.geometry, At(0.0f, 0.0f, 0.25f, 0.40f),
                                                   monitor.pixels);
            const float expected = (0.25f / 0.40f) * (monitor.pixels.width / monitor.pixels.height);
            CheckNear(box.aspect, expected, 1e-4f * std::max(1.0f, expected),
                      std::string("像素宽高比 = (归一化宽/高) × (屏幕宽/高),屏幕 ") +
                          monitor.label);
        }
    }

    // ---- 3. 没声明就说没声明 ----
    {
        const std::optional<float> none;
        auto free = ::miaodesk::desktop::ShippedLike("free", 0.30f, 0.30f, false, nullptr);
        const auto verdict = JudgeWidgetAspect(free.geometry, At(0.0f, 0.0f, 0.30f, 0.30f), fhd);
        Check(!verdict.declared, "没声明 aspectRatio 时 verdict.declared 为假");
        Check(verdict.exact, "没声明时不得报'不一致'");
        CheckNear(verdict.distortion, 1.0f, 0.0f, "没声明时不得报失真");
    }

    // ---- 4. 信箱化:内盒必须在盒子里,并且真的保住比例 ----
    {
        const auto box = ResolveWidgetPixelBox(
            ::miaodesk::desktop::ShippedLike("c", 0.30f, 0.30f, false, nullptr).geometry,
            At(0.0f, 0.0f, 0.30f, 0.30f), fhd);
        const auto fitted = FitAspectInside(box, 1.0f);
        CheckNear(fitted.height, box.height, 1e-4f, "1:1 时装不满宽度,高度保持");
        CheckNear(fitted.width, box.height, 1e-4f, "宽度收到与高度相同(324)");
        CheckNear(fitted.aspect, 1.0f, 1e-5f, "内盒比例就是声明值");
        CheckNear(fitted.x, box.x + (box.width - fitted.width) * 0.5f, 1e-3f, "水平居中留边");
        CheckNear(fitted.y, box.y, 1e-3f, "高度方向没有留边");
        Check(fitted.x >= box.x && fitted.y >= box.y, "内盒不越出盒子左上");
        Check(fitted.x + fitted.width <= box.x + box.width + 1e-3f, "内盒不越出右边界");
        Check(fitted.y + fitted.height <= box.y + box.height + 1e-3f, "内盒不越出下边界");

        // 内盒比原盒"更瘦"时(0.5 < 1.778):高度用满,宽度收窄,左右留边。
        const auto tall = FitAspectInside(box, 0.5f);
        CheckNear(tall.height, box.height, 1e-4f, "0.5:1 时装不满宽度,高度用满");
        CheckNear(tall.width, box.height * 0.5f, 1e-3f, "宽度收到高度的一半(162)");
        CheckNear(tall.aspect, 0.5f, 1e-4f, "内盒比例就是声明值");
        CheckNear(tall.x, box.x + (box.width - tall.width) * 0.5f, 1e-3f, "水平居中留边");
        CheckNear(tall.y, box.y, 1e-3f, "高度方向没有留边");

        // 内盒比原盒"更扁"时(2.0 > 1.778):宽度用满,高度收窄,上下留边。
        const auto wide = FitAspectInside(box, 2.0f);
        CheckNear(wide.width, box.width, 1e-4f, "2:1 时装不满高度,宽度用满");
        CheckNear(wide.height, box.width / 2.0f, 1e-3f, "高度收到宽度的一半(288)");
        CheckNear(wide.aspect, 2.0f, 1e-4f, "内盒比例就是声明值");
        CheckNear(wide.y, box.y + (box.height - wide.height) * 0.5f, 1e-3f, "垂直居中留边");

        // 极端比例仍然只留边,不越界。
        const auto sliver = FitAspectInside(box, 0.1f);
        CheckNear(sliver.aspect, 0.1f, 1e-4f, "0.1:1 时内盒仍是 0.1");
        Check(sliver.height <= box.height + 1e-3f && sliver.width <= box.width + 1e-3f,
              "再窄也不越出盒子");
        CheckNear(sliver.y, box.y, 1e-3f, "瘦到极端时上下不留边(留不出内容来)");

        // 非法比例与非法盒子原样返回,不许 NaN。
        const auto bad = FitAspectInside(box, 0.0f);
        CheckNear(bad.width, box.width, 0.0f, "比例 0 原样返回");
        const auto nan = FitAspectInside(box, std::numeric_limits<float>::quiet_NaN());
        CheckNear(nan.width, box.width, 0.0f, "NaN 比例原样返回");
        WidgetPixelBox empty;
        const auto none = FitAspectInside(empty, 2.0f);
        CheckNear(none.width, 0.0f, 0.0f, "空盒子上信箱化仍返回空盒子");
    }

    // ---- 5. 极端屏幕与极端输入不许 NaN ----
    {
        auto d = ::miaodesk::desktop::ShippedLike("d", 0.30f, 0.30f, true, nullptr);
        const MonitorPixels screens[] = {{0.0f, 0.0f},
                                         {1920.0f, 0.0f},
                                         {std::numeric_limits<float>::quiet_NaN(), 1080.0f},
                                         {3840.0f, 2160.0f},
                                         {1080.0f, 1920.0f}};
        for (const auto& screen : screens) {
            const auto box = ResolveWidgetPixelBox(d.geometry, At(0.0f, 0.0f, 0.30f, 0.30f), screen);
            Check(std::isfinite(box.width) && std::isfinite(box.height) && std::isfinite(box.x) &&
                      std::isfinite(box.y) && std::isfinite(box.aspect) &&
                      std::isfinite(box.screenFraction),
                  "任何屏幕尺寸都得到有限的像素盒(0×0、单边 0、NaN 都不许)");
        }
        // NaN 位置不许变成 NaN 坐标。
        const auto nanPos = ResolveWidgetPixelBox(d.geometry, At(1e30f, -1e30f, 0.30f, 0.30f), fhd);
        Check(std::isfinite(nanPos.x) && nanPos.x >= 0.0f, "横坐标被夹回屏内");
        Check(nanPos.y == 0.0f, "负坐标被夹回 0");
        // 越界的盒子夹回屏内,而不是返回一个宿主建不出渲染目标的尺寸。
        const auto overflow = ResolveWidgetPixelBox(d.geometry, At(0.9f, 0.9f, 0.30f, 0.30f), fhd);
        Check(overflow.x + overflow.width <= 1920.0f + 0.01f, "右边界越界时位置夹回");
        Check(overflow.y + overflow.height <= 1080.0f + 0.01f, "下边界越界时位置夹回");
    }

    // ---- 6. 固定尺寸的组件忽略 instance 里那份(ValidateInstance 已保证它俩相等) ----
    {
        const std::optional<float> none;
        auto fixed = ::miaodesk::desktop::ShippedLike("fixed", 0.30f, 0.30f, false, nullptr);
        const auto box = ResolveWidgetPixelBox(fixed.geometry, At(0.0f, 0.0f, 0.90f, 0.90f), fhd);
        CheckNear(box.width, 576.0f, 0.01f, "固定尺寸组件用声明值,不用 instance 值");
        CheckNear(box.height, 324.0f, 0.01f, "高同样用声明值");
    }

    if (g_failures != 0) {
        std::printf("\n失败 %d / %d\n", g_failures, g_checks);
        return 1;
    }
    std::printf("\n组件几何:%zu 块参考屏幕,全部 %d 项检查通过\n", kReferenceMonitorCount, g_checks);
    return 0;
}
