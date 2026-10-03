#include "miaodesk/MiaoTurnLifecycle.h"

namespace miaodesk::turn_lifecycle {

const wchar_t* TurnPhaseName(TurnPhase phase) noexcept {
    switch (phase) {
    case TurnPhase::Idle: return L"Idle";
    case TurnPhase::Running: return L"Running";
    case TurnPhase::Stopping: break;
    }
    return L"Stopping";
}

TurnStartVerdict JudgeTurnStart(TurnPhase phase) noexcept {
    TurnStartVerdict verdict;
    if (phase == TurnPhase::Idle) {
        verdict.allowed = true;
        return verdict;
    }
    verdict.allowed = false;
    // 前缀固定(见 kTurnRejectionPrefix 的契约说明),两档的差异只追加在后头。
    verdict.reason = kTurnRejectionPrefix;
    if (phase == TurnPhase::Stopping) {
        verdict.reason += L":正在取消上一轮,请稍候再发";
    } else {
        verdict.reason += L":上一轮还在进行中";
    }
    return verdict;
}

TurnPhase PhaseAfterStopRequest(TurnPhase phase) noexcept {
    // Idle 上取消是空操作:这里不许凭空造出一个"忙"。
    if (phase == TurnPhase::Idle) return TurnPhase::Idle;
    // Running 与 Stopping 都进 Stopping。第二次取消不改变状态 —— 幂等。
    return TurnPhase::Stopping;
}

TurnPhase PhaseAfterWorkerExit(TurnPhase phase) noexcept {
    // 只有 worker 真的退完才能回 Idle。Running 上"worker 退出"是不该出现的情形
    // (worker 自己结束就意味着这一轮跑完了,正常路径上也该走这里),同样回 Idle。
    (void)phase;
    return TurnPhase::Idle;
}

bool StopRequestIsIdempotent(TurnPhase phase) noexcept {
    // 空相位上取消不改变任何东西;其余两个都收敛到同一个 Stopping。
    const TurnPhase first = PhaseAfterStopRequest(phase);
    const TurnPhase second = PhaseAfterStopRequest(first);
    return first == second;
}

bool TurnTimeline::Start() noexcept {
    const TurnPhase before = phase_;
    const TurnStartVerdict verdict = JudgeTurnStart(before);
    if (!verdict.allowed) {
        ++rejected_;
        return false;
    }
    phase_ = TurnPhase::Running;
    ++started_;
    ++inFlight_;
    return true;
}

void TurnTimeline::StopRequest() noexcept {
    phase_ = PhaseAfterStopRequest(phase_);
}

void TurnTimeline::WorkerExited() noexcept {
    if (inFlight_ > 0) --inFlight_;
    // 还有已取消但没退完的轮次时,留在 Stopping,不回 Idle。
    phase_ = inFlight_ > 0 ? TurnPhase::Stopping : PhaseAfterWorkerExit(phase_);
}

} // namespace miaodesk::turn_lifecycle
