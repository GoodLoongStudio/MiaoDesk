#pragma once

// WPRO-05 组件更新调度与能耗:一个组件下一次该什么时候重画。
//
// 为什么要单独一份:宿主的 `RepaintDue` 按 `nextRefreshAt == 0 || now >= nextRefreshAt`
// 决定要不要画,而 `nextRefreshAt` 只在一次**成功**的 `PaintSlot` 末尾被
// `ScheduleNextRefresh` 设上。`PaintSlot` 有五条提前返回的路径(渲染目标拿不到、
// 渲染器加载失败、宿主数据应用失败、Draw 返回 false、Present 失败),它们都**跳过**
// 那一行 —— 于是 `nextRefreshAt` 一直是 0,而 0 的含义是"随时都该画"。
//
// 后果:一个画不出来的组件每 16 ms 重试一次,一秒 60 次,一天 520 万次,
// 每次都重跑一遍渲染目标/渲染器/宿主数据链路。用户看不见任何变化,只看见风扇转。
// 这不是假设 —— `nextRefreshAt` 的赋值点只有 `ScheduleNextRefresh`(成功路径)、
// `SetPaused`、`WeatherDataChanged` 与"设备丢失重来"四处,失败路径一处都没有。
//
// 这个头文件只做决定:输入是"上一次成功了吗、连错几次、内容要多快的帧",
// 输出是"下一次该在多久之后画"。纯逻辑,不碰盘、不 import Windows 头,于是
// "失败会退避、退避有上限、成功会立刻恢复、绝不返回'随时都画'"这些性质在本机可验。
//
// 边界:它不知道时钟。宿主拿返回值去加 GetTickCount64();时钟在那里,不在这里。
#include <cstdint>

namespace miaodesk::desktop {

// 组件的刷新调度状态。宿主每个 slot 存一份。
struct WidgetRefreshState {
    // 已经连续失败多少次。成功一次就归零。
    std::uint32_t consecutiveFailures{};
    // 上一次算出来的延迟(毫秒)。调用方可用于诊断,不参与计算。
    std::uint32_t lastDelayMs{};
};

// 空闲内容的最低刷新间隔:内容不要求连续帧时,仍然按这个间隔重画一次。
inline constexpr std::uint32_t kWidgetIdleRefreshMs = 1000;

// 直接呈现(swapchain)组件的心跳:不能完全睡着,但远低于每帧。
inline constexpr std::uint32_t kWidgetDirectSurfaceHeartbeatMs = 2000;

// 失败退避的起点与上限。
inline constexpr std::uint32_t kWidgetFailureBackoffStartMs = 1000;
inline constexpr std::uint32_t kWidgetFailureBackoffCeilingMs = 30000;

// 退避倍率。2 → 1s、2s、4s、8s…到 30s 封顶共 5 次翻倍。
inline constexpr std::uint32_t kWidgetFailureBackoffFactor = 2;

// 一次成功的重画之后,下一次该在多久之后。
//
// demandIntervalMs > 0 时按内容要的间隔(帧调度器给的 animationFps 换算值);
// 为 0 表示内容不要连续帧,于是按 idle 间隔 —— 直接呈现的组件再夹一次心跳。
// directPresentation 只影响空闲间隔,不影响内容主动要帧的间隔。
std::uint32_t NextRefreshDelayAfterSuccess(std::uint32_t demandIntervalMs,
                                           bool directPresentation) noexcept;

// 一次失败的重画之后,下一次该在多久之后。
//
// 指数退避,从 kWidgetFailureBackoffStartMs 翻倍到 kWidgetFailureBackoffCeilingMs 封顶。
// 连错一次就退避,是因为"再来一次可能正好"在 60Hz 下等于不停试。
// 返回值**永不为 0**:0 在宿主那里的意思是"随时都该画",而那正是要修的东西。
std::uint32_t NextRefreshDelayAfterFailure(const WidgetRefreshState& state) noexcept;

// 把一次结果记进状态并给出下一次延迟。宿主只需要调这一个。
std::uint32_t ApplyRefreshOutcome(WidgetRefreshState* state, bool succeeded, std::uint32_t demandIntervalMs,
                                  bool directPresentation) noexcept;

// 从现在起,这个 slot 下一次该在什么时候画(host 时钟)。
std::uint64_t NextRefreshAt(std::uint64_t nowMs, std::uint32_t delayMs) noexcept;

} // namespace miaodesk::desktop
