// WPRO-01 组件几何:归一化数在真实屏幕上到底是多大。
//
// 值得单独测的理由:geometry 全用归一化写,于是"这个组件有多大"在写 manifest 时是
// 答不上来的;而产品此前没有任何地方把它算出来过。本模块把它算出来,于是
// "manifest 承诺正方形的组件,在 16:9 屏幕上其实是 1.78:1"这件事在本机就能看见。
//
// 这里验的是算术,不验渲染:真正把它落到像素要改宿主,那需要 Windows。
#include "miaodesk/DesktopWidgetStore.h"
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
using namespace miaodesk::wallpaper;

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

    // ---- 7. 给宿主的重载:直接吃真实像素盒 ----
    {
        const std::optional<float> square = 1.0f;
        // 与"从归一化反推"必须给出同一个答案 —— 两条路算的是同一件事,
        // 分歧只应来自宿主真实的盒子与推算不同(而那本身就是该报的信息)。
        auto clock = ::miaodesk::desktop::ShippedLike("c", 0.30f, 0.30f, false, &square);
        const auto box = ResolveWidgetPixelBox(clock.geometry, At(0.0f, 0.0f, 0.30f, 0.30f),
                                               MonitorPixels{1920.0f, 1080.0f});
        const auto viaBox = JudgeWidgetAspectOfBox(box, clock.geometry.aspectRatio);
        const auto viaNormalized =
            JudgeWidgetAspect(clock.geometry, At(0.0f, 0.0f, 0.30f, 0.30f),
                              MonitorPixels{1920.0f, 1080.0f});
        Check(viaBox.pixelAspect == viaNormalized.pixelAspect && viaBox.exact == viaNormalized.exact &&
                  viaBox.distortion == viaNormalized.distortion,
              "重载与从归一化反推给出一致结论(同一个算术,两条入口)");
        Check(!viaBox.exact, "FHD 上重载也报'不成立'(576×324 对 1.0)");
        CheckNear(viaBox.distortion, 1.7778f, 1e-3f, "重载报同一个 1.78 失真");

        // 宿主要能通过它报出"实际盒子和推算不一样"。
        WidgetPixelBox clamped = box;
        clamped.height = 300.0f;  // 宿主要紧一截(DPI、边界夹紧)
        const auto differ = JudgeWidgetAspectOfBox(clamped, square);
        Check(!differ.exact && std::fabs(differ.pixelAspect - viaBox.pixelAspect) > 1e-6f,
              "盒子与推算不同时,裁决跟着变(这正是诊断要的那个信息)");

        // 没声明时同样不许报'不一致'。
        const std::optional<float> none;
        const auto undeclared = JudgeWidgetAspectOfBox(box, none);
        Check(!undeclared.declared && undeclared.exact && undeclared.distortion == 1.0f,
              "重载在没声明时也不报不一致");

        // 空盒子不许 NaN。
        WidgetPixelBox empty;
        const auto onEmpty = JudgeWidgetAspectOfBox(empty, square);
        Check(onEmpty.declared && !onEmpty.exact,
              "空盒子上仍给出结论(不成立),而不是 NaN");
        Check(std::isfinite(onEmpty.pixelAspect) && onEmpty.pixelAspect == 0.0f,
              "空盒子的实际比例是 0,不是 NaN");
    }

    // ---- 8. FormatAspect:给人看的一小段 ----
    {
        Check(FormatAspect(1.7778f) == L"1.778", "1.7778 印成 1.778");
        Check(FormatAspect(1.0f) == L"1.000", "1.0 印成 1.000(与 1.778 并排时才对得齐)");
        Check(FormatAspect(0.0f) == L"0.000", "0 印成 0.000");
        Check(FormatAspect(-2.5f) == L"-2.500", "负数照印,不取绝对值");
        Check(FormatAspect(std::numeric_limits<float>::quiet_NaN()) == L"nan", "NaN 印成 nan");
        Check(FormatAspect(std::numeric_limits<float>::infinity()) == L"nan", "inf 也印成 nan");
        // 三位小数足够分辨 1.778 与 1.333,也足够分辨 1.000 与 1.001。
        Check(FormatAspect(1.0004f) == L"1.000" && FormatAspect(1.0006f) == L"1.001",
              "三位小数能分辨容差边界上的差别");
    }

    // ---- 9. 桌面组件实例几何(P0-04 生命周期不变量) ----
    {
        // 先把常量本身钉住。少了这一段,"尺寸下限改成 0"这种破坏测不出来 ——
        // 下面的断言拿同一个常量做比较,常量改断言也跟着改,于是永远成立。
        CheckNear(kWidgetLayoutMaxPosition, 0.95f, 0.0f, "位置上限是 0.95(不是 1.0)");
        CheckNear(kWidgetLayoutMinSize, 0.05f, 0.0f, "尺寸下限是 0.05(不是 0)");
        CheckNear(kWidgetLayoutDefaultX, 0.68f, 0.0f, "默认 x 是 0.68");
        CheckNear(kWidgetLayoutDefaultY, 0.05f, 0.0f, "默认 y 是 0.05");
        CheckNear(kWidgetLayoutDefaultWidth, 0.28f, 0.0f, "默认宽是 0.28");
        CheckNear(kWidgetLayoutDefaultHeight, 0.18f, 0.0f, "默认高是 0.18");

        // 默认值:结构体的成员默认值与常量同源,这样"新建组件落在哪里"有唯一答案。
        WidgetLayout plain;
        CheckNear(plain.x, 0.68f, 0.0f, "默认 x 与 DesktopWidget 一致");
        CheckNear(plain.y, 0.05f, 0.0f, "默认 y 一致");
        CheckNear(plain.width, 0.28f, 0.0f, "默认 width 一致");
        CheckNear(plain.height, 0.18f, 0.0f, "默认 height 一致");
        const auto untouched = NormalizeWidgetLayout(plain);
        CheckNear(untouched.x, 0.68f, 0.0f, "归一化不动合法值");
        CheckNear(untouched.width, 0.28f, 0.0f, "合法尺寸不变");

        // 越界被夹回,而且**夹回之后仍满足 x+width<=1**。
        WidgetLayout bad;
        bad.x = 1.4f;
        bad.y = -3.0f;
        bad.width = 5.0f;
        bad.height = -1.0f;
        const auto clamped = NormalizeWidgetLayout(bad);
        Check(clamped.x >= 0.0f && clamped.x <= kWidgetLayoutMaxPosition, "x 夹进 [0, 0.95]");
        Check(clamped.y >= 0.0f && clamped.y <= kWidgetLayoutMaxPosition, "y 夹进 [0, 0.95]");
        Check(clamped.x + clamped.width <= 1.0f, "夹回后 x+width<=1(组件不会整个落桌面外)");
        Check(clamped.y + clamped.height <= 1.0f, "夹回后 y+height<=1");
        Check(clamped.width >= kWidgetLayoutMinSize, "宽度不小于下限");
        Check(clamped.height >= kWidgetLayoutMinSize, "高度不小于下限");

        // 边界恰好贴住 1.0 的情形(0.95 + 0.05):不许因为浮点误差变成 1.0000001。
        WidgetLayout edge;
        edge.x = 0.95f;
        edge.y = 0.95f;
        edge.width = 0.05f;
        edge.height = 0.05f;
        const auto atEdge = NormalizeWidgetLayout(edge);
        Check(atEdge.x + atEdge.width <= 1.0f + 1e-6f, "贴边时 x+width 不越界");
        Check(atEdge.width >= kWidgetLayoutMinSize - 1e-6f, "贴边时宽度不跌破下限");

        // 这一批是修的真实缺陷:NaN 会被 std::clamp 原样放行。
        const float nans[] = {std::numeric_limits<float>::quiet_NaN(),
                              -std::numeric_limits<float>::quiet_NaN()};
        const float infs[] = {std::numeric_limits<float>::infinity(),
                              -std::numeric_limits<float>::infinity()};
        for (float nan : nans) {
            for (float inf : infs) {
                WidgetLayout broken;
                broken.x = nan;
                broken.y = inf;
                broken.width = nan;
                broken.height = inf;
                const auto fixed = NormalizeWidgetLayout(broken);
                Check(std::isfinite(fixed.x) && std::isfinite(fixed.y) &&
                          std::isfinite(fixed.width) && std::isfinite(fixed.height),
                      "NaN/inf 一律换成有限数(std::clamp 对 NaN 是恒等函数,挡不住)");
                Check(fixed.x + fixed.width <= 1.0f, "NaN 尺寸夹回后不越界");
                Check(fixed.width >= kWidgetLayoutMinSize, "NaN 尺寸夹回后不小于下限");
            }
        }
        // 单独一个 NaN,其余正常:只换那一个。
        WidgetLayout partial;
        partial.x = 0.3f;
        partial.y = 0.3f;
        partial.width = std::numeric_limits<float>::quiet_NaN();
        partial.height = 0.2f;
        const auto oneFixed = NormalizeWidgetLayout(partial);
        Check(std::isfinite(oneFixed.width) && oneFixed.x == 0.3f && oneFixed.height == 0.2f,
              "只换坏掉的那一个字段,不牵连正常字段");

        // 恰好 0 与极小尺寸:随机采样几乎产不出**正好** 0(那样破坏尺寸下限也测不出来),
        // 所以显式走一遍。0 宽的组件点不中,而用户只会以为"桌面卡了"。
        const float zeros[] = {0.0f, 1e-9f, -0.0f, 1e-30f};
        for (float zero : zeros) {
            WidgetLayout collapsed;
            collapsed.x = 0.0f;
            collapsed.y = 0.0f;
            collapsed.width = zero;
            collapsed.height = zero;
            const auto revived = NormalizeWidgetLayout(collapsed);
            Check(revived.width >= kWidgetLayoutMinSize && revived.height >= kWidgetLayoutMinSize,
                  "0 / 极小尺寸被抬到下限之上(点不中的组件比没有更糟)");
        }

        // 不变量:对一大批随机输入,输出永远满足全部四条。随机而不是举八个例子,
        // 是因为"夹紧之后会不会跌破下限"取决于顺序,而顺序只有一个。
        std::uint32_t seed = 12345;
        auto next = [&seed]() {
            seed = seed * 1664525u + 1013904223u;
            return static_cast<float>(static_cast<std::int32_t>(seed)) / 2147483647.0f;
        };
        for (int i = 0; i < 2000; ++i) {
            WidgetLayout random;
            random.x = next() * 3.0f - 1.0f;
            random.y = next() * 3.0f - 1.0f;
            random.width = next() * 3.0f;
            random.height = next() * 3.0f;
            const auto out = NormalizeWidgetLayout(random);
            if (!(out.x >= 0.0f && out.x <= kWidgetLayoutMaxPosition && out.y >= 0.0f &&
                  out.y <= kWidgetLayoutMaxPosition && out.width >= kWidgetLayoutMinSize &&
                  out.height >= kWidgetLayoutMinSize && out.x + out.width <= 1.0f + 1e-6f &&
                  out.y + out.height <= 1.0f + 1e-6f)) {
                Check(false, "随机输入下四条不变量全部成立(2000 个样本)");
                break;
            }
        }
        Check(true, "随机输入下四条不变量全部成立(2000 个样本)");

        // 幂等:归一化两次等于归一化一次。宿主每次 Load/Save 都会过一遍,
        // 不幂等会让组件每存一次就挪一点。
        WidgetLayout drift;
        drift.x = 0.9f;
        drift.width = 0.4f;
        const auto once = NormalizeWidgetLayout(drift);
        const auto twice = NormalizeWidgetLayout(once);
        Check(once.x == twice.x && once.y == twice.y && once.width == twice.width &&
                  once.height == twice.height,
              "归一化幂等(存两次不该让组件挪动)");
    }
    // ---- 10. Native 组件去重键(P0-04 "无重复实例") ----
    {
        auto widgetOf = [](DesktopWidgetKind kind, const wchar_t* source, const wchar_t* monitor) {
            DesktopWidget w;
            w.id = L"w";
            w.kind = kind;
            w.source = source;
            w.monitorId = monitor ? std::wstring(monitor) : std::wstring();
            return w;
        };

        // source 从预设表里取,不写字面量 —— 写字面量的测试会在表改名之后悄悄全绿
        // (它比的根本不是表里的值)。第一版我就写了 native://glass-clock,而表里是
        // native:glass-clock,于是三条断言全红。
        const std::wstring clock = NativePresetSource(NativeWidgetPreset::GlassClock);
        const std::wstring weather = NativePresetSource(NativeWidgetPreset::WeatherGlass);
        Check(!clock.empty(), "预设表里真的有 GlassClock(否则下面全是空断言)");

        // 同一个 preset、空 monitorId(主显示器):那就是同一个实例。
        const auto a = widgetOf(DesktopWidgetKind::Native, clock.c_str(), nullptr);
        const auto b = widgetOf(DesktopWidgetKind::Native, clock.c_str(), nullptr);
        Check(SameNativeSingleton(a, b), "同 preset 同主显示器判为同一个实例");
        Check(!NativeSingletonKey(a).empty(), "Native 预设参与去重(键非空)");

        // 同一个 preset、不同显示器:允许各放一个。多显示器用户期望的行为,
        // 去重键里带上 monitorId 就是为了它。
        const auto other = widgetOf(DesktopWidgetKind::Native, clock.c_str(), L"monitor-2");
        Check(!SameNativeSingleton(a, other), "同 preset 不同显示器不算重复(多屏各放一个)");

        // 大小写不敏感:monitorId 的采集路径不同,同一个物理屏可能被拼成两种写法。
        const auto upper = widgetOf(DesktopWidgetKind::Native, clock.c_str(), L"MONITOR-2");
        const auto lower = widgetOf(DesktopWidgetKind::Native, clock.c_str(), L"monitor-2");
        Check(SameNativeSingleton(upper, lower), "monitorId 大小写不敏感(采集路径不一致)");

        // source 本身也大小写不敏感。
        const auto shouty = widgetOf(DesktopWidgetKind::Native, L"NATIVE:GLASS-CLOCK", nullptr);
        Check(SameNativeSingleton(a, shouty), "source 大小写不敏感(预设查找本来就不区分)");

        // 不同 preset:永远不重复。
        const auto otherPreset = widgetOf(DesktopWidgetKind::Native, weather.c_str(), nullptr);
        Check(!SameNativeSingleton(a, otherPreset), "不同 preset 不重复");

        // Content 组件允许同一定义多开:三块屏幕各放一个天气组件是正常用法。
        const auto contentA = widgetOf(DesktopWidgetKind::Content, L"content:com.goodloong.weather", nullptr);
        const auto contentB = widgetOf(DesktopWidgetKind::Content, L"content:com.goodloong.weather", nullptr);
        Check(NativeSingletonKey(contentA).empty(), "Content 组件键为空(不参与去重)");
        Check(!SameNativeSingleton(contentA, contentB), "同一定义的 Content 组件可以多开");
        Check(!SameNativeSingleton(contentA, a), "Content 与 Native 之间不构成重复");

        // Unknown 与空 source:不参与去重,也不许崩。
        const auto unknown = widgetOf(DesktopWidgetKind::Unknown, L"", nullptr);
        Check(!SameNativeSingleton(unknown, unknown), "Unknown 类型不参与去重");
        const auto emptySource = widgetOf(DesktopWidgetKind::Native, L"", nullptr);
        Check(!SameNativeSingleton(emptySource, emptySource), "空 source 不参与去重");
    }


    if (g_failures != 0) {
        std::printf("\n失败 %d / %d\n", g_failures, g_checks);
        return 1;
    }
    std::printf("\n组件几何:%zu 块参考屏幕,全部 %d 项检查通过\n", kReferenceMonitorCount, g_checks);
    return 0;
}
