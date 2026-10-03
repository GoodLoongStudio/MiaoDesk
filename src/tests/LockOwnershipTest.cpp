// P0-08 的后半句:无不可恢复单实例锁。
//
// 现在全仓判"该不该再起一个"只靠 `OpenMutexW` 能不能打开。它只回答"锁在不在",
// 不回答"它属于谁、那个进程还活着吗" —— 于是持有者卡住但没死时,宿主一直以为
// 渲染进程在跑,而没有任何地方记着持有者的 PID,连"该杀谁"都答不出来。
//
// 这个文件钉住"把那个布尔换成带理由的裁决"这一层的契约。它不含产品决策:
// 卡住的 owner 只被**报告**,不被自动接管 —— 接管动作是产品决策,留在此处之外。
//
// 反空洞自检恒返回 Available 的函数在下面每组断言上同样全绿,所以开头先喂一个
// 明知该判 Wedged 的观测、一个明知该判 Running 的观测,确认裁决真的会动。
#include "miaodesk/MiaoLockOwnership.h"

#include <cstdio>
#include <string>

namespace miaodesk::lock_ownership {
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

LockObservation MakeObs(std::uint32_t pid, std::uint64_t heartbeat, std::uint64_t now,
                        std::uint64_t lease, bool alive) {
    LockObservation obs;
    obs.recordPresent = pid != 0;
    obs.record.pid = pid;
    obs.record.heartbeatSeconds = heartbeat;
    obs.ownerProcessAlive = alive;
    obs.nowSeconds = now;
    obs.leaseSeconds = lease;
    return obs;
}

bool VerdictStillMoves() {
    const LockObservation healthy = MakeObs(4242, 1000, 1005, 30, true);
    const LockObservation wedged = MakeObs(4242, 900, 1005, 30, true);
    return JudgeLockOwnership(healthy) == LockVerdict::Running &&
           JudgeLockOwnership(wedged) == LockVerdict::Wedged;
}

} // namespace
} // namespace miaodesk::lock_ownership

int wmain() {
    using namespace miaodesk;
    using namespace miaodesk::lock_ownership;

    if (!VerdictStillMoves()) {
        std::printf("\n[FAIL] 反空洞自检没通过:裁决分不出 healthy 与 wedged\n");
        return 1;
    }
    std::printf("  [PASS] 反空洞自检:心跳新鲜判 Running、过期判 Wedged\n");
    ++g_checks;

    const std::uint64_t kNow = 100000;
    const std::uint64_t kLease = 60;

    // ---- 1. 没有持有者 ----
    {
        const auto obs = MakeObs(0, 0, kNow, kLease, false);
        Check(JudgeLockOwnership(obs) == LockVerdict::Available, "没有记录就是 Available");
        Check(MayStartNewOwner(LockVerdict::Available), "没有持有者可以起");
    }

    // ---- 2. 健康的持有者 ----
    {
        // 心跳刚好新于租约。
        const auto fresh = MakeObs(4242, kNow - kLease, kNow, kLease, true);
        Check(JudgeLockOwnership(fresh) == LockVerdict::Running, "心跳在租约内判 Running");
        Check(!MayStartNewOwner(LockVerdict::Running), "它在跑,不许再起一个");

        // 边界:刚好等于租约仍算新鲜(不是"超过")。边界写成 `>` 而不是 `>=` ——
        // 用 `>=` 会让"心跳刚满租约"这一瞬间被误判成卡住。
        const auto exact = MakeObs(4242, kNow - kLease, kNow, kLease, true);
        Check(JudgeLockOwnership(exact) == LockVerdict::Running,
              "心跳恰好等于租约时长仍算新鲜(超过才算失联)");
    }

    // ---- 3. 卡住但没死 —— 这正是 OpenMutexW 判不出来的那一类 ----
    {
        const auto wedged = MakeObs(4242, kNow - kLease - 1, kNow, kLease, true);
        const auto verdict = JudgeLockOwnership(wedged);
        Check(verdict == LockVerdict::Wedged, "★ 活着但心跳过期判 Wedged(P0-08 的核心)");
        Check(!MayStartNewOwner(verdict), "卡住的 owner 也不许起第二个(资源还被占着)");
        // 裁决必须说得出人话,而且要点出是哪个进程、多久没心跳、租约多长。
        const std::wstring text = DescribeLockVerdict(wedged, verdict);
        Check(text.find(L"4242") != std::wstring::npos, "原因里点出是哪个进程");
        Check(text.find(L"61") != std::wstring::npos, "原因里说出多久没有心跳");
        Check(text.find(L"任务管理器") != std::wstring::npos,
              "原因告诉用户怎么办(而不是静默地以为一切正常)");
        Check(text.find(L"不会自动接管") != std::wstring::npos,
              "原因说清当前不做动作 —— 那是产品决策,不能悄悄做掉");
    }

    // ---- 4. 记录在、进程不在了 ----
    {
        const auto stale = MakeObs(777, kNow - 10, kNow, kLease, false);
        Check(JudgeLockOwnership(stale) == LockVerdict::StaleRecord, "记录在但进程没了判 StaleRecord");
        Check(MayStartNewOwner(LockVerdict::StaleRecord), "进程没了可以起新的");
        Check(stale.HasRecord(), "pid 非空就算有记录(心跳为 0 也算 —— 那是没有心跳,不是没有持有者)");
    }

    // ---- 5. 观测不足以裁决:绝不当成 Available ----
    {
        LockObservation obs;
        obs.leaseSeconds = 0;                       // 租约没配
        obs.recordPresent = false;
        Check(JudgeLockOwnership(obs) == LockVerdict::Unusable,
              "没配租约时拒绝裁决,而不是默默用一个默认租约");
        Check(!MayStartNewOwner(LockVerdict::Unusable), "判不了就不许起");

        // 锁存在但没有任何记录 —— 记录这一侧坏了,而锁是真实的。按不可判处理,
        // 不按"没有持有者"处理:后者会让人以为这里判过。
        LockObservation lockOnly;
        lockOnly.recordPresent = true;              // 锁在
        lockOnly.record.pid = 0;                    // 但没有身份记录
        lockOnly.leaseSeconds = kLease;
        Check(JudgeLockOwnership(lockOnly) == LockVerdict::Unusable,
              "锁存在但没有身份记录时按不可判处理(不是 Available)");

        // pid=0 而 recordPresent=false 才是真的没有 owner。
        LockObservation none;
        none.recordPresent = false;
        none.leaseSeconds = kLease;
        Check(JudgeLockOwnership(none) == LockVerdict::Available, "真的没有 owner 才是 Available");
    }

    // ---- 6. 时钟倒退不许把健康判成卡住 ----
    // 进程挂了又重启、或时钟被往回拨时,心跳可能大于 now。这里必须收敛到
    // "距上次心跳 0 秒",而不是做无符号下减变成一个天文数字。
    {
        const auto skew = MakeObs(4242, kNow + 500, kNow, kLease, true);
        Check(JudgeLockOwnership(skew) == LockVerdict::Running,
              "心跳时间戳晚于 now(时钟倒退)时判 Running,不是天文数字般的失联");
    }

    // ---- 7. 幂等:同一条观测判两次,结论相同 ----
    {
        const auto obs = MakeObs(4242, kNow - 500, kNow, kLease, true);
        const auto first = JudgeLockOwnership(obs);
        const auto second = JudgeLockOwnership(obs);
        Check(first == second, "同一观测重复判定收敛");
        Check(first == LockVerdict::Wedged, "且结论是 Wedged");
    }

    // ---- 8. 五种裁决的名字都不是空的 ----
    {
        for (const auto verdict : {LockVerdict::Available, LockVerdict::Running,
                                   LockVerdict::Wedged, LockVerdict::StaleRecord,
                                   LockVerdict::Unusable}) {
            Check(LockVerdictName(verdict) != nullptr && LockVerdictName(verdict)[0] != L'\0',
                  "每个裁决都有非空名字(日志/诊断要能印出来)");
        }
    }

    if (g_failures != 0) {
        std::printf("\n失败 %d / %d\n", g_failures, g_checks);
        return 1;
    }
    std::printf("\n锁持有权裁决:全部 %d 项通过\n", g_checks);
    return 0;
}
