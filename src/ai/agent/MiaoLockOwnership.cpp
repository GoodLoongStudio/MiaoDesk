#include "miaodesk/MiaoLockOwnership.h"

namespace miaodesk::lock_ownership {

const wchar_t* LockVerdictName(LockVerdict verdict) noexcept {
    switch (verdict) {
    case LockVerdict::Available: return L"Available";
    case LockVerdict::Running: return L"Running";
    case LockVerdict::Wedged: return L"Wedged";
    case LockVerdict::StaleRecord: return L"StaleRecord";
    case LockVerdict::Unusable: break;
    }
    return L"Unusable";
}

LockVerdict JudgeLockOwnership(const LockObservation& observation) noexcept {
    // 没配租约就拒绝裁决。这里**不许**给一个默认租约:一个默默生效的默认值会让人
    // 以为这里配过,而"多久算失联"恰恰是这条链上最该被显式决定的一件事。
    if (observation.leaseSeconds == 0) return LockVerdict::Unusable;

    if (!observation.HasRecord()) {
        // 从未有人记录过 owner。注意这与"锁存在但没有记录"是两件事:后者说明
        // 记录这一侧坏了,而锁是真实的 —— 那种情形按 Unusable 处理更安全。
        return observation.recordPresent ? LockVerdict::Unusable : LockVerdict::Available;
    }

    if (!observation.ownerProcessAlive) {
        // 记录在,而它说的进程不在了。内核通常会连句柄一起收掉,所以这条路很少走到;
        // 走到了就说明记录与真实世界不一致 —— 说得出的不一致比静默的不一致好。
        return LockVerdict::StaleRecord;
    }

    // 失联判定用"距上次心跳多久",不用"进程在不在"—— 后者对卡死的进程永远为真,
    // 而那正是当前 `OpenMutexW` 判不出来的那一类。
    const std::uint64_t elapsed = observation.nowSeconds > observation.record.heartbeatSeconds
                                      ? observation.nowSeconds - observation.record.heartbeatSeconds
                                      : 0;
    return elapsed > observation.leaseSeconds ? LockVerdict::Wedged : LockVerdict::Running;
}

bool MayStartNewOwner(LockVerdict verdict) noexcept {
    // Wedged 与 Running 都不许起:一个是它在跑,另一个是它卡着但还占着资源。
    // 接管卡住的 owner 需要"杀一个还活着的进程",那是产品决策,不在这份判定里。
    return verdict == LockVerdict::Available || verdict == LockVerdict::StaleRecord;
}

std::wstring DescribeLockVerdict(const LockObservation& observation, LockVerdict verdict) noexcept {
    switch (verdict) {
    case LockVerdict::Available:
        return L"没有持有者,可以启动";
    case LockVerdict::Running:
        return L"持有者进程 " + std::to_wstring(observation.record.pid) + L" 心跳正常,已在运行";
    case LockVerdict::Wedged:
        return L"持有者进程 " + std::to_wstring(observation.record.pid) +
               L" 还活着但已 " +
               std::to_wstring(observation.nowSeconds > observation.record.heartbeatSeconds
                                   ? observation.nowSeconds - observation.record.heartbeatSeconds
                                   : 0) +
               L" 秒没有心跳(租约 " + std::to_wstring(observation.leaseSeconds) +
               L" 秒):它卡住了。当前不会自动接管,请从任务管理器结束它后重试。";
    case LockVerdict::StaleRecord:
        return L"锁记录说进程 " + std::to_wstring(observation.record.pid) +
               L" 持有,但它已经不存在;可以启动新的。";
    case LockVerdict::Unusable:
        break;
    }
    return L"锁状态无法判定";
}

} // namespace miaodesk::lock_ownership
