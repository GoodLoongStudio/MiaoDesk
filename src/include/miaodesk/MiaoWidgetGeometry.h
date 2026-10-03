#pragma once

// WPRO-01 组件尺寸族与自适应布局:归一化几何在真实屏幕上到底是多大。
//
// 为什么要有这个头文件:`geometry` 全是用**归一化**数写成的(defaultWidth 0.30 是
// 桌面宽度的 30%),于是"这个组件有多大"这个问题,在写 manifest 的时候是答不上来的 ——
// 答案取决于用户那块屏幕。而产品里此前没有任何地方把它算出来过:
//
//   · `Manifest` 写 `aspectRatio: 1.0`,读起来像"这是一个正方形组件";
//   · `MiaoContentModel::ValidateInstance` 拿 `instance.width / instance.height`
//     (0.30 / 0.30 = 1.0)去比,于是校验通过;
//   · 而宿主要的像素盒是**归一化宽 × 屏幕宽**、**归一化高 × 屏幕高**。
//     1920×1080 上就是 576×324 —— 比例 1.78。
//
// 也就是说:一个声明"正方形"的组件,在 16:9 屏幕上是 1.78:1 的长条,而 manifest 的校验
// 一路绿灯。这不是假设,是本轮把四个随产品发行的组件逐个算过之后的结论。
// 同一个字段在三个地方各说各话,而用户只看见屏幕上那一个。
//
// 这个模块只做算术:不碰盘、不 import Windows 头、不画一个像素。于是"组件在每块屏幕上
// 到底多大、比例对不对"在本机就能真验,并且能作为发行内容的门。
//
// 边界:它**不改变渲染行为**。真正把宽高比落到像素上(信箱化留边)要动宿主与渲染器,
// 那需要 Windows 上看得见效果才敢签收;这里先把"现在实际是多少"变成可测、可见、可门的。
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "miaodesk/MiaoContentModel.h"

namespace miaodesk::desktop {

// 一块屏幕的物理像素尺寸。
struct MonitorPixels {
    float width{1920.0f};
    float height{1080.0f};
};

// 归一化几何在某块屏幕上真实占用的像素盒。
struct WidgetPixelBox {
    float x{};
    float y{};
    float width{};
    float height{};
    // width / height。两边都是 0 时给 0,不许 NaN。
    float aspect{};
    // 占屏幕面积的比例。给"这个组件大不大"一个与屏幕分辨率无关的答案。
    float screenFraction{};
};

// 归一化几何 → 像素盒。
//
// 越界的实例会被夹回屏内(而不是返回一个不可能的值):宿主要拿这个数去建渲染目标,
// 一个超出屏幕的盒子会让 UpdateLayeredWindow 直接失败。夹紧的策略是优先保住左上角,
// 因为那正是用户拖动时最先看的地方。
WidgetPixelBox ResolveWidgetPixelBox(
    const content::ContentGeometryPolicy& geometry,
    const content::ContentInstance& instance,
    const MonitorPixels& monitor) noexcept;

// 声明的宽高比与实际得到的宽高比,差多少。
struct WidgetAspectVerdict {
    // 这个组件有没有声明 aspectRatio。没声明时下面三个数都不该被当成结论读。
    bool declared{};
    float declaredAspect{};
    // 归一化盒直接映射到像素后得到的比例。
    float pixelAspect{};
    // pixelAspect / declaredAspect。>1 被拉宽,<1 被压扁,1 正好。
    // 没声明时是 1(无从谈起失真)。
    float distortion{};
    // 是否在容差内相等。容差见 kWidgetAspectTolerance。
    bool exact{};
};

WidgetAspectVerdict JudgeWidgetAspect(
    const content::ContentGeometryPolicy& geometry,
    const content::ContentInstance& instance,
    const MonitorPixels& monitor) noexcept;

// 宽高比判定认为"相等"的容差。
//
// 固定相对容差而不是绝对容差:比例可以从 0.05(细条)到 20(横幅),一个绝对容差
// 在两头会同时失准。1e-3 留给浮点与分数夹紧的余地 —— 但注意:**它能容忍的失真
// 在 1.78 对 1.00 面前没有任何意义**,那个缺口是 44%,不是第 4 位小数。
inline constexpr float kWidgetAspectTolerance = 1e-3f;

// 在像素盒内取得保留 aspect 的内盒,居中留边(信箱化)。
//
// 这是"把 manifest 里那句话落到像素上"的那个函数。当前**没有任何调用方** ——
// 宿主按它改建渲染目标的时机属于 WPRO-01 的实施一轮。单独给出来是因为它必须是
// 纯算术才能在本机验;而等它接进宿主时,这条"留边居中、不裁内容、不越出盒子"
// 的契约应当已经被测过了。
//
// 内盒完全放进原盒子里(不越界),并且居中 —— 左右或上下留边,不会两边都留。
// aspect <= 0 或盒子非法时原盒返回。
WidgetPixelBox FitAspectInside(const WidgetPixelBox& box, float aspect) noexcept;

// 常出现的屏幕尺寸。不是穷举,是"用户机器上真正多的那几档":把 21:9 与 16:10 都放进来,
// 是因为宽高比的失真恰恰是随屏幕比例变的,只测 16:9 会漏掉最极端的那些。
struct ReferenceMonitor {
    const char* label;
    MonitorPixels pixels;
};

inline constexpr std::array<ReferenceMonitor, 8> kReferenceMonitors = {{
    {"FHD 16:9", {1920.0f, 1080.0f}},
    {"QHD 16:9", {2560.0f, 1440.0f}},
    {"UHD 16:9", {3840.0f, 2160.0f}},
    {"Ultrawide 21:9", {3440.0f, 1440.0f}},
    {"Super-ultrawide 32:9", {5120.0f, 1440.0f}},
    {"MacBook 16:10", {3456.0f, 2160.0f}},
    {"Square-ish 4:3", {1600.0f, 1200.0f}},
    {"Upright 9:16", {1080.0f, 1920.0f}},
}};

inline constexpr std::size_t kReferenceMonitorCount = kReferenceMonitors.size();

} // namespace miaodesk::desktop
