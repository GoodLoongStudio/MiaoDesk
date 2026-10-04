// WPRO-05 组件刷新调度:失败必须退避,成功必须立刻恢复,绝不返回"随时都画"。
//
// 这个文件修的是一个会一直烧 CPU 的问题:宿主的 nextRefreshAt 只在成功绘制时被赋值,
// 失败路径一条都不赋值,而 0 的含义是"随时都该画"。于是画不出来的组件每 16 ms 重试一次,
// 一秒 60 次。这里把"下一次多久之后画"的决定抽成纯逻辑,于是上面那三条性质每轮都验。
//
// 判据刻意按"用户可感知的最小变化"选:
//   · 失败退避 —— 退避期间没有任何新信息,重试只是空转;
//   · 成功即恢复 —— 恢复之后如果还按退避间隔走,一次内容变化会晚最多 30 秒才上屏,
//     那比"多画几帧"糟得多;
//   · 绝不为 0 —— 0 在宿主那里的含义正是要修的那个 bug。
#include "miaodesk/WidgetRefreshPolicy.h"

#include <cstdio>
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

} // namespace
} // namespace miaodesk

int wmain() {
    using namespace miaodesk;
    using namespace miaodesk::desktop;

    // ---- 1. 成功路径:内容要帧就按它要的,不要帧就按 idle ----
    Check(NextRefreshDelayAfterSuccess(16, false) == 16, "内容要 16ms 一帧就给 16ms");
    Check(NextRefreshDelayAfterSuccess(9, false) == 9, "120fps 下给 9ms");
    Check(NextRefreshDelayAfterSuccess(0, false) == kWidgetIdleRefreshMs,
          "内容不要连续帧时按 idle 间隔(1000ms)");
    Check(NextRefreshDelayAfterSuccess(0, true) == kWidgetDirectSurfaceHeartbeatMs,
          "直接呈现的组件 idle 时夹一次心跳(2000ms)");
    Check(NextRefreshDelayAfterSuccess(16, true) == 16,
          "内容主动要帧时不受 directPresentation 影响(心跳只约束空闲)");

    // ---- 2. 失败路径:指数退避到上限 ----
    WidgetRefreshState failing;
    // 起点 2000 而不是 1000:见 kWidgetFailureBackoffStartMs 上的说明 ——
    // NativeWidgetHost 的刷新 tick 是 1000ms,起点取 1000 的话"第一次失败就退避"
    // 实际上是第一次失败毫无变化。
    const std::uint32_t expected[] = {2000, 4000, 8000, 16000, 30000, 30000, 30000, 30000};
    for (std::uint32_t i = 0; i < 8; ++i) {
        const std::uint32_t delay = NextRefreshDelayAfterFailure(failing);
        Check(delay == expected[i],
              "连续失败第 " + std::to_string(i + 1) + " 次退避到 " + std::to_string(expected[i]) +
                  "ms(实际 " + std::to_string(delay) + ")");
        failing.consecutiveFailures += 1;
    }
    // 上限就是上限:再失败也不许更久 —— 一个组件坏了 10 分钟之后重新出现时,
    // 它得在 30 秒内自己回来,而不是等到天荒地老。
    failing.consecutiveFailures = 1000;
    Check(NextRefreshDelayAfterFailure(failing) == kWidgetFailureBackoffCeilingMs,
          "失败次数很大时仍夹在上限(30s),不许无限增长");
    // 0 次失败(还没失败过)也要给一个非 0 的起点。
    WidgetRefreshState fresh;
    Check(NextRefreshDelayAfterFailure(fresh) == kWidgetFailureBackoffStartMs,
          "还没失败过时也给起点间隔,不是 0");

    // ---- 3. 核心:任何输入下延迟都不为 0 ----
    for (std::uint32_t failures = 0; failures < 200; ++failures) {
        WidgetRefreshState state;
        state.consecutiveFailures = failures;
        const std::uint32_t delay = NextRefreshDelayAfterFailure(state);
        if (delay == 0) {
            Check(false, "退避延迟永不为 0(0 在宿主那里的含义是'随时都画')");
            break;
        }
    }
    for (std::uint32_t demand = 0; demand < 500; demand += 7) {
        Check(NextRefreshDelayAfterSuccess(demand, false) > 0, "成功延迟永不为 0");
        Check(NextRefreshDelayAfterSuccess(demand, true) > 0, "直接呈现下成功延迟也不为 0");
    }
    Check(NextRefreshAt(1000, 0) > 1000, "即使给了 0 延迟,下一帧也必须在未来");

    // ---- 4. 成功立刻恢复:退避之后一次成功必须回到内容要的间隔 ----
    {
        WidgetRefreshState state;
        state.consecutiveFailures = 5;  // 已经在 16s 退避
        state.lastDelayMs = 16000;
        const std::uint32_t delay = ApplyRefreshOutcome(&state, true, 16, false);
        Check(delay == 16, "退避中一次成功立刻回到内容要的 16ms(不能还按 16s 走)");
        Check(state.consecutiveFailures == 0, "成功把连续失败计数归零");
        Check(state.lastDelayMs == 16, "状态记下这次的延迟");
    }
    {
        WidgetRefreshState state;
        state.consecutiveFailures = 5;
        const std::uint32_t delay = ApplyRefreshOutcome(&state, true, 0, false);
        Check(delay == kWidgetIdleRefreshMs, "空闲内容恢复后回到 1000ms,不是 30s");
    }

    // ---- 5. ApplyRefreshOutcome:第一次失败就退到起点,不是翻倍后的起点 ----
    {
        // 这个 off-by-one 我第一版真写成了:先 ++ 再算,于是"第一次失败"拿到的是 2000ms。
        // 用户感知是"组件刚坏的那一秒重试了 1 次而不是 2 次",不重要;
        // 重要的是"退避起点"这个常量从此名不副实,而断言说它必须是 1000。
        WidgetRefreshState state;
        const std::uint32_t first = ApplyRefreshOutcome(&state, false, 0, false);
        Check(first == kWidgetFailureBackoffStartMs,
              "第一次失败退避到起点(必须 > NativeWidgetHost 的 1000ms tick,否则第一步不降频)");
        Check(state.consecutiveFailures == 1, "第一次失败后计数为 1");
        const std::uint32_t second = ApplyRefreshOutcome(&state, false, 0, false);
        Check(second == 4000, "第二次失败退避到 4000ms");
        const std::uint32_t third = ApplyRefreshOutcome(&state, false, 0, false);
        Check(third == 8000, "第三次失败退避到 8000ms");
        Check(state.lastDelayMs == 8000, "状态记下最近一次延迟");
    }

    // ---- 6. 状态指针为空时不许崩,并退回成功路径的答案 ----
    Check(ApplyRefreshOutcome(nullptr, false, 0, false) == kWidgetIdleRefreshMs,
          "状态指针为空时不许崩,按成功路径给值");
    Check(ApplyRefreshOutcome(nullptr, true, 25, false) == 25, "空指针 + 内容要 25ms 给 25ms");

    // ---- 7. 退避真的把重试次数降下来了 ----
    {
        // 冷启动第一分钟:退避还在往上爬(2s、6s、14s、30s、60s),所以有 5 次,
        // 而不是今天的 3750 次。 ramp-up 与稳态不能混为一谈 —— 我第一版就写成了
        // "一分钟最多 2 次",那是稳态的数字,拿它去判冷启动当然红。
        // 起点从 1s 抬到 2s 之后这个数从 6 变成 5:同一次失败的第一次重试晚了 1s,
        // 冷启动一分钟里就少一次。这是抬起点换来的,方向是对的。
        WidgetRefreshState state;
        std::uint64_t simulated = 0;
        int attempts = 0;
        for (int i = 0; i < 1000 && simulated < 60000; ++i) {
            simulated += ApplyRefreshOutcome(&state, false, 0, false);
            ++attempts;
        }
        Check(attempts == 5, "冷启动第一分钟重试 5 次(退避还在爬),不是 3750 次");
        Check(attempts < 20, "第一分钟的重试次数有界(而不是每 16ms 一次)");

        // 稳态:已经爬到 30s 之后,每分钟最多 2 次。
        WidgetRefreshState steady;
        steady.consecutiveFailures = 100;
        std::uint64_t minute = 0;
        int steadyAttempts = 0;
        for (int i = 0; i < 1000 && minute < 60000; ++i) {
            minute += ApplyRefreshOutcome(&steady, false, 0, false);
            ++steadyAttempts;
        }
        Check(steadyAttempts == 2, "稳态下一分钟重试 2 次(30s 封顶),不是 3750 次");
    }

    if (g_failures != 0) {
        std::printf("\n失败 %d / %d\n", g_failures, g_checks);
        return 1;
    }
    std::printf("\n组件刷新调度:全部 %d 项检查通过\n", g_checks);
    return 0;
}
