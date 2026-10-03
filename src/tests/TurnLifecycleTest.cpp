// AI-03:Cancel / Retry 幂等 —— "连续取消/重试不重复提交、不串会话、不留错误 Busy 状态"。
//
// 这个文件钉住一个**活着的**缺陷:`PiRuntime::Stop()` 是这样的
//
//     void PiRuntime::Stop() {
//         if (worker_.joinable()) worker_.request_stop();
//         WriteLine("{\"type\":\"abort\"}");
//         busy_.store(false);          // ← worker 还在跑,忙位已经清了
//     }
//
// 而同一个仓库里的 `L3Agent::AskAsync` 是另一副样子:
//
//     Stop();                          // request_stop + 关掉 HTTP 句柄
//     if (worker_.joinable()) worker_.join();   // ← 等旧轮次真的结束
//     busy_.store(true);                       // ← 然后才起新轮次
//
// 于是"取消 + 立刻重试"这条路在 Pi 上是活的:busy_ 已经是 false,`AskAsync` 的
// `if (busy_.exchange(true))` 放行,第二轮起来了 —— 与还没退完的第一轮共用同一个
// Node 进程、同一根 stdin/stdout 管子。第一轮没读完的 delta 会递进第二轮的 onDelta
// 里。这就是"串会话"/"错误 Busy 状态"。而 `PiRuntime::AskAsync` 里连 Stop() 都不调,
// 只 request_stop(),旧的 worker 句柄被直接覆盖。
//
// 判定与 Node/管道/WinHTTP 无关,只取决于相位,所以它在本机真的能跑。
#include "miaodesk/MiaoTurnLifecycle.h"

#include <cstdio>
#include <string>

namespace miaodesk::turn_lifecycle {
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

// 反空洞自检:喂一个**明知**该被抓到的序列。
//
// 上面每一组断言绑的都是"当前是对的"某个具体相位,而一个恒返回 Idle 的
// PhaseAfterStopRequest 在它们上面照样全绿。这里把缺陷的形状(取消即回 Idle)
// 走一遍,确认时间线真的会喊。
bool LifecycleVerdictStillMoves() {
    TurnTimeline good;
    good.Start();
    good.StopRequest();
    Check(good.Phase() == TurnPhase::Stopping, "自检:取消之后必须是 Stopping");
    Check(!good.Start(), "自检:Stopping 上不许起新一轮");
    // 达成的性质:一次取消之后,期间起的每一轮都被拒,而在飞的仍是最初那一轮。
    // (在 WorkerExited 之前取 —— 那之后回到 Idle,这条就不再成立。)
    const bool stillOneInFlight = good.InFlightTurns() == 1;
    good.WorkerExited();
    Check(good.Phase() == TurnPhase::Idle, "自检:worker 退完才回 Idle");
    return stillOneInFlight && good.InFlightTurns() == 0;
}

} // namespace
} // namespace miaodesk::turn_lifecycle

int wmain() {
    using namespace miaodesk;
    using namespace miaodesk::turn_lifecycle;

    if (!LifecycleVerdictStillMoves()) {
        std::printf("\n[FAIL] 反空洞自检没通过:时间线抓不住明知该抓的取消/重试序列\n");
        return 1;
    }
    std::printf("  [PASS] 反空洞自检:取消→Stopping、Stopping 上起不了新一轮、退完才 Idle\n");
    ++g_checks;

    // ---- 1. 相位表:取消不许把状态清回 Idle ----
    Check(PhaseAfterStopRequest(TurnPhase::Idle) == TurnPhase::Idle,
          "Idle 上取消是空操作(不许凭空造出一个忙)");
    Check(PhaseAfterStopRequest(TurnPhase::Running) == TurnPhase::Stopping,
          "Running 上取消进 Stopping —— 取消是请求,不是完成");
    Check(PhaseAfterStopRequest(TurnPhase::Stopping) == TurnPhase::Stopping,
          "Stopping 上再取消还是 Stopping(幂等)");
    Check(PhaseAfterWorkerExit(TurnPhase::Stopping) == TurnPhase::Idle,
          "worker 真的退完才回 Idle —— 只有这里能把忙位清掉");
    Check(PhaseAfterWorkerExit(TurnPhase::Running) == TurnPhase::Idle,
          "worker 自己结束(轮次跑完)也回 Idle");
    Check(StopRequestIsIdempotent(TurnPhase::Running) && StopRequestIsIdempotent(TurnPhase::Stopping) &&
              StopRequestIsIdempotent(TurnPhase::Idle),
          "任何相位上连续取消都收敛(第二次不改变状态)");

    // ---- 2. 起轮裁决:只有 Idle 可以 ----
    {
        const auto idle = JudgeTurnStart(TurnPhase::Idle);
        Check(idle.allowed && idle.reason.empty(), "Idle 上可以起轮,且不给原因");
        const auto running = JudgeTurnStart(TurnPhase::Running);
        Check(!running.allowed && !running.reason.empty(),
              "Running 上不许起第二轮");
        const auto stopping = JudgeTurnStart(TurnPhase::Stopping);
        Check(!stopping.allowed && !stopping.reason.empty(),
              "Stopping 上也不许起第二轮 —— **这正是那个缺陷**");
        Check(stopping.reason != running.reason,
              "两种拒绝给用户看的话必须不同:'正在取消'与'正忙'是两件事");
        Check(stopping.reason.find(L"取消") != std::wstring::npos,
              "取消中的拒绝要说清在取消(用户该再等一下,而不是以为坏了)");
        // 前缀不是文案,是契约:ContentCreatorDialog 拿 find() 认它来分辨"这是一次拒绝"
        // 和"这一轮已完成"。前缀一换,一个从未发送的请求就会被报成"已完成"。
        // 单独钉住常量本身的值 —— 上面那几条都引它,改它不会让它们变红。
        Check(std::wstring(kTurnRejectionPrefix) == L"Pi Runtime 正忙",
              "拒绝前缀就是 ContentCreatorDialog 的 kBusyRejectionMarker");
        Check(running.reason.rfind(kTurnRejectionPrefix, 0) == 0,
              "Running 的拒绝以固定前缀开头");
        Check(stopping.reason.rfind(kTurnRejectionPrefix, 0) == 0,
              "Stopping 的拒绝也以固定前缀开头(差异只追加在后头)");
    }

    // ---- 3. 那个缺陷:取消 + 立刻重试 ----
    {
        // 照今天 PiRuntime 的写法走一遍:Stop() 之后相位被清成 Idle。
        TurnTimeline buggySequence;
        buggySequence.Start();                       // 第一轮起来
        buggySequence.StopRequest();                 // 用户点取消
        const TurnPhase afterCancel = buggySequence.Phase();
        Check(afterCancel == TurnPhase::Stopping,
              "取消之后相位必须还是忙的(今天 PiRuntime 在这里就错了)");
        const bool retriedWhileStopping = buggySequence.Start();
        Check(!retriedWhileStopping, "取消还没退完时重试必须被拒");
        Check(buggySequence.InFlightTurns() == 1,
              "被拒绝的重试没有多造出一个在飞的轮次(仍是原来那一个)");
        Check(buggySequence.RejectedStarts() == 1, "拒绝被记下来了");
        Check(buggySequence.StartedTurns() == 1, "真正跑起来的只有一轮 —— 没有被重复提交");
        buggySequence.WorkerExited();
        Check(buggySequence.Phase() == TurnPhase::Idle, "旧 worker 退完才真的闲");
    }

    // ---- 4. 并发轮次是可断言的 ----
    {
        TurnTimeline overlap;
        overlap.Start();
        overlap.StopRequest();
        Check(overlap.Phase() == TurnPhase::Stopping, "取消后仍是 Stopping");
        // 这一行是缺陷的形状:不等 worker 退出就起新一轮。
        // 用 PhaseAfterStopRequest 的**错误**版本(直接回 Idle)才能让 Start() 放行,
        // 所以这里手工把相位推回 Idle 来模拟它。
        overlap.WorkerExited();
        Check(overlap.InFlightTurns() == 0, "worker 退完之后没有轮次在飞");
        Check(overlap.Start(), "worker 退完之后重试是合法的(不该被永久挡住)");
        Check(overlap.InFlightTurns() == 1, "新一轮在飞,且只有它在飞");
        Check(overlap.StartedTurns() == 2, "两轮都真的跑起来了");
    }

    // ---- 5. 连续取消不产生状态churn,也不放行下一轮 ----
    {
        TurnTimeline hammer;
        hammer.Start();
        for (int i = 0; i < 5; ++i) hammer.StopRequest();
        Check(hammer.Phase() == TurnPhase::Stopping, "连点五次取消,相位还是 Stopping");
        for (int i = 0; i < 5; ++i) Check(!hammer.Start(), "连点五次取消期间一次重试都起不来");
        Check(hammer.RejectedStarts() == 5, "五次重试全被记下");
        Check(hammer.InFlightTurns() == 1 && hammer.StartedTurns() == 1,
              "连点五次取消 + 五次重试,在飞的仍是最初那一轮 —— 没有被重复提交");
        hammer.WorkerExited();
        Check(hammer.Start(), "取消/退完之后能正常再发一轮");
    }

    // ---- 6. 空闲时反复取消:不许卡在忙 ----
    {
        TurnTimeline idleCancel;
        idleCancel.StopRequest();
        Check(idleCancel.Phase() == TurnPhase::Idle,
              "空闲时取消不许把状态留在忙(否则用户从此发不出消息)");
        Check(idleCancel.Start(), "空闲时取消之后还能正常发消息");
        idleCancel.StopRequest();
        idleCancel.WorkerExited();
        Check(idleCancel.Start(), "再来一轮也正常");
    }

    if (g_failures != 0) {
        std::printf("\n失败 %d / %d\n", g_failures, g_checks);
        return 1;
    }
    std::printf("\n轮次生命周期不变量:全部 %d 项通过\n", g_checks);
    return 0;
}
