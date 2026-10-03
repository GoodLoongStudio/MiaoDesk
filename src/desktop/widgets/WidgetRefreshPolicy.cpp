#include "miaodesk/WidgetRefreshPolicy.h"

#include <algorithm>

namespace miaodesk::desktop {

std::uint32_t NextRefreshDelayAfterSuccess(std::uint32_t demandIntervalMs,
                                           bool directPresentation) noexcept {
    if (demandIntervalMs > 0) return demandIntervalMs;
    const std::uint32_t idle = directPresentation ? kWidgetDirectSurfaceHeartbeatMs
                                                  : kWidgetIdleRefreshMs;
    return idle;
}

std::uint32_t NextRefreshDelayAfterFailure(const WidgetRefreshState& state) noexcept {
    // delay 从 kWidgetFailureBackoffStartMs 起、只乘不除,所以它不可能变成 0。
    // 这里**没有**"最后一个 if (delay == 0) delay = start"那样的兜底:变异检测
    // 把它删掉测试仍然全绿,说明它挡的是一件不可能发生的事,而一行挡不住任何东西的
    // 代码只会让人以为这里有过一个 0 的案例。"延迟永不为 0"由
    // WidgetRefreshPolicyTest 里两条遍历循环从外侧钉住。
    std::uint32_t delay = kWidgetFailureBackoffStartMs;
    for (std::uint32_t step = 0; step < state.consecutiveFailures; ++step) {
        if (delay >= kWidgetFailureBackoffCeilingMs) break;
        delay *= kWidgetFailureBackoffFactor;
    }
    if (delay > kWidgetFailureBackoffCeilingMs) delay = kWidgetFailureBackoffCeilingMs;
    return delay;
}

std::uint32_t ApplyRefreshOutcome(WidgetRefreshState* state, bool succeeded,
                                  std::uint32_t demandIntervalMs, bool directPresentation) noexcept {
    if (!state) return NextRefreshDelayAfterSuccess(demandIntervalMs, directPresentation);
    std::uint32_t delay = 0;
    if (succeeded) {
        state->consecutiveFailures = 0;
        delay = NextRefreshDelayAfterSuccess(demandIntervalMs, directPresentation);
    } else {
        // 先按"此前已经失败了几次"算延迟,再记这次。
        // 反过来写会让第一次失败就翻一倍(2s 而不是 1s)—— 我第一次就是这么写的,
        // 是断言"退避起点 1000ms"把它逮住的。
        delay = NextRefreshDelayAfterFailure(*state);
        state->consecutiveFailures += 1;
    }
    state->lastDelayMs = delay;
    return delay;
}

std::uint64_t NextRefreshAt(std::uint64_t nowMs, std::uint32_t delayMs) noexcept {
    return nowMs + (delayMs == 0 ? 1u : delayMs);
}

} // namespace miaodesk::desktop
