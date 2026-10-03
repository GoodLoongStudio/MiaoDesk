#pragma once

// P0-08 的后半句:"无不可恢复单实例锁"。
//
// 现状:全仓 5 处 `CreateMutexW`,而"该不该再起一个"全部靠同一个判据 ——
// `OpenMutexW` 能不能打开。它只回答"锁在不在",**不回答"它属于谁、那个进程还活着吗"**。
//
// 于是持有者**卡住但没死**时,`NamedMutexExists` 一直返回 true,启动点一直
// `return true`,宿主一直以为渲染进程在跑,而**没有任何地方记着持有者的 PID** ——
// 连"该杀谁"都答不出来。用户只能手工开任务管理器。
//
// 关键的一步:把那个布尔换成**带理由的裁决**。这一步不含任何产品决策 ——
// 它只是把"healthy owner / wedged owner / no owner"分开,让宿主第一次能说出口
// "壁纸渲染进程没有响应",而不是静默地以为一切正常。
// (接管动作 —— 要不要 TerminateProcess 一个还活着的进程 —— 才是产品决策,不在这份判定里。)
//
// 纯逻辑:不碰盘、不碰 OS 句柄,只取决于一条记录 + 一次观测,所以本机就能真跑、真门。
#include <cstdint>
#include <string>

namespace miaodesk::lock_ownership {

// 一条"谁持有这把锁"的记录。锁本身之外另存的一份身份。
struct LockRecord {
    // 持有者进程 ID。0 表示没人记录过。
    std::uint32_t pid{};
    // 最后一次心跳的 Unix 秒。0 表示没有心跳。
    std::uint64_t heartbeatSeconds{};
};

// 启动前对这把锁做的一次观测。
struct LockObservation {
    // 记录在不在(命名对象之外的另存身份)。false 表示从来没有 owner。
    bool recordPresent{};
    LockRecord record{};
    // 记录里那个进程现在是否还活着。recordPresent 为 false 时忽略。
    bool ownerProcessAlive{};
    // 当前 Unix 秒。
    std::uint64_t nowSeconds{};
    // 租约时长:心跳超过它没更新就算失联。0 或负值视为未配置 —— 那时本模块拒绝裁决,
    // 而不是默默用一个默认值(那会让人以为这里配过)。
    std::uint64_t leaseSeconds{};

    bool HasRecord() const noexcept { return recordPresent && record.pid != 0; }
};

enum class LockVerdict {
    // 没有 owner,可以起。
    Available,
    // owner 活着且心跳新鲜 —— 不要再起一个。
    Running,
    // owner 活着但心跳过期 —— 它卡住了。**只报告,不动作**:接管策略是产品决策。
    Wedged,
    // 记录在,但它说的那个进程已经不在了。内核其实会连句柄一起收掉,这条路通常
    // 走不到;显式说出来是为了让"记录与真实世界不一致"这件事可见。
    StaleRecord,
    // 观测不足以裁决(租约没配)。绝不当成 Available —— 那会让人以为这里判过。
    Unusable,
};

const wchar_t* LockVerdictName(LockVerdict verdict) noexcept;

// 给用户/日志看的一句话。空表示没有可说的。
std::wstring DescribeLockVerdict(const LockObservation& observation, LockVerdict verdict) noexcept;

// 判一次:这把锁现在是什么状态。
LockVerdict JudgeLockOwnership(const LockObservation& observation) noexcept;

// 这条观测能不能安全地起一个新 owner。
// 只有 Available 与 StaleRecord 可以 —— 两者都不需要动任何活着的进程。
bool MayStartNewOwner(LockVerdict verdict) noexcept;

} // namespace miaodesk::lock_ownership
