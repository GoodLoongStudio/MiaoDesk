#pragma once

// AI-03:Cancel / Retry 必须是幂等的 —— "连续取消/重试不重复提交、不串会话、
// 不留错误 Busy 状态"。
//
// 为什么要有这个文件:仓库里有两个 AI 运行时,而它们对"取消"的处理**不一样**。
//
//   · `L3Agent::AskAsync` 先 `Stop()` 再 `worker_.join()`,**等旧轮次真的结束**之后
//     才 `busy_ = true` 起新轮次;
//   · `PiRuntime::Stop()` 只做 `request_stop()` + 往管道里写一句 abort,然后
//     **立刻** `busy_ = false` —— worker 还在跑,新的一轮却已经可以开始了。
//
// 于是一次"取消 + 重试"之后:
//     busy_ == false                              ← UI 说"闲了",而旧轮次还在跑
//     AskAsync 的 busy_.exchange(true) 守卫放行    ← 第二轮起来了
//     两轮共用同一个 Node 进程、同一根 stdin/stdout 管子
// 旧轮次还没读完的 delta 会递进新轮次的 onDelta 里 —— 这正是"串会话"。
// 而 `PiRuntime::AskAsync` 里连 `Stop()` 都不调,只 `worker_.request_stop()`,
// 于是旧的 worker 句柄被直接覆盖、连取消都不取消。
//
// 抽成纯逻辑是因为这条判定与 Node、管道、WinHTTP 全都无关:它只取决于相位。
// 真机上重现它需要一次恰有竞争的取消/重试;而相位不变量每轮都能在本机验。
#include <string>

namespace miaodesk::turn_lifecycle {

// 一轮对话的生命周期相位。
//
// Running 与 Stopping 分开是**这个模块存在的理由**:"已请求取消、worker 还没退"
// 本身仍然是忙。把它们合成一个 Idle/Running 两态,正是那个缺陷的形状 ——
// 取消的那一刻就没什么能拦住下一轮了。
enum class TurnPhase {
    // 没有轮次在跑。只有这里能起新轮次。
    Idle,
    // 一轮在跑。
    Running,
    // 已请求取消,worker 还没退。**仍然算忙。**
    Stopping,
};

const wchar_t* TurnPhaseName(TurnPhase phase) noexcept;

// 一个新轮次能不能开始的裁决。
struct TurnStartVerdict {
    bool allowed{};
    // 拒绝原因,给用户看的一句话。allowed 为 true 时为空。
    std::wstring reason;
};

// 拒绝语的前缀。**这是一个跨模块契约,不是一句文案**:
// `ContentCreatorDialog` 的 kBusyRejectionMarker 就是这个串,它拿 find() 判断
// "这个 done 是一次拒绝而不是一次完成" —— 前缀一改,一个从未发送的请求就会被当成
// "本轮生成已完成"报给用户。tests/content-creator-modes.mjs 与 TurnLifecycleTest
// 两头都钉住它。
inline constexpr wchar_t kTurnRejectionPrefix[] = L"Pi Runtime 正忙";

// 一个新轮次能不能开始。只有 Idle 可以。
//
// Stopping 与 Running 分开报是有意的:用户取消之后马上重试,界面上该说的是
// "正在取消上一轮,请稍候再发"而不是光光的"正忙" —— 前者告诉他再等一下就好,
// 后者像是个故障。但**前缀必须保持 kTurnRejectionPrefix**,差异只追加在后头。
TurnStartVerdict JudgeTurnStart(TurnPhase phase) noexcept;

// 请求取消之后的新相位。
//
// 这个函数是整个修法的关键:**它不许返回 Idle**。取消是一次请求,不是一次完成。
// worker 自己退出时才会回 Idle(见 PhaseAfterWorkerExit)。
TurnPhase PhaseAfterStopRequest(TurnPhase phase) noexcept;

// worker 真的退完之后的新相位。只有这里能把 busy 清掉。
TurnPhase PhaseAfterWorkerExit(TurnPhase phase) noexcept;

// 连续取消是否幂等:第二次取消不许改变任何状态。
bool StopRequestIsIdempotent(TurnPhase phase) noexcept;

// 一条取消/重试序列的时间线,以及"有没有出现过并发轮次"的结论。
//
// 用它而不是只测上面那些纯函数,是因为缺陷要一整串动作才看得见:单独一个
// Stop()→Idle 的映射看不出任何问题,只有"取消之后又起了一轮"才出事。
// 时间线把这条序列走完,并给出可断言的结论。
class TurnTimeline {
public:
    // 起一轮。被拒时返回 false 且记一次拒绝,**不会**多造出一个在飞的轮次。
    bool Start() noexcept;
    // 请求取消。
    void StopRequest() noexcept;
    // worker 退出。
    void WorkerExited() noexcept;

    TurnPhase Phase() const noexcept { return phase_; }
    // 已起轮、但 worker 还没退完的轮次数。
    //
    // **这里故意没有一个"曾经并发过"的布尔量。** 第一版有,而变异检测逮到它是不可达的:
    // 取消进 Stopping 挡住起轮,只有 WorkerExited 回 Idle,而它在回 Idle 之前已经把 inFlight_
    // 减掉了 —— 于是"起轮时 inFlight_ > 0"这个状态凭正确的 API 根本走不到。
    // 一个永远为 false 的探测量与没有它长得一模一样。真正该断言的、**可达**的那条是:
    // 被拒绝的起轮不会多造出一个在飞的轮次。用下面三个计数器就能说清。
    unsigned InFlightTurns() const noexcept { return inFlight_; }
    // 被拒绝的启动次数。
    unsigned RejectedStarts() const noexcept { return rejected_; }
    // 真正跑起来的轮次数。
    unsigned StartedTurns() const noexcept { return started_; }

private:
    TurnPhase phase_{TurnPhase::Idle};
    // 已起轮但 worker 还没退完的轮次数。
    unsigned inFlight_{0};
    unsigned started_{0};
    unsigned rejected_{0};
};

} // namespace miaodesk::turn_lifecycle
