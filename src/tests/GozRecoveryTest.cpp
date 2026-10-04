// SEARCH-04「Goz 服务故障自动恢复」可恢复那一半的判定回归。
//
// 逮到的不是"它会算错",而是**它在本机一行都验不到,而且每一步观测都被当场丢掉**:
// `GozSearch::EnsurePipeAvailable` 一路上看得见每个环节(SCM 打不打得开、服务在不在、
// 状态是什么、StartService 成没成、等到没有),但它只回一个 bool。于是
// `MiaoFileSearchNotice` 的第三桶只能说"可能服务没起来,也可能查询超时" ——
// 一句诚实的猜测,不是诊断。
//
// 这里钉住:Win32 状态数值的映射、九种结局各自可达、以及**不许对已经在起的服务
// 再叫一次 StartService**(那会拿到 ERROR_SERVICE_ALREADY_RUNNING,把结论搅浑)。
#include "miaodesk/MiaoGozRecovery.h"

#include <cstdio>
#include <string>

namespace miaodesk {
namespace goz_recovery {
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

// 最常见的一组观测:服务停着、拉起成功、等到了。
GozRecoveryOutcome Recovered(bool startIssued = true) {
    return DecideGozRecovery(false, true, true, GozServiceState::Stopped, startIssued, true);
}

// 反空洞自检。两个方向各喂一个:一个什么都判成 Recovered 的判定会让
// "服务没装"显示成"已恢复",一个什么都判成 TimedOut 的判定会让真好了的也报超时。
bool VerdictStillMoves() {
    return Recovered() == GozRecoveryOutcome::Recovered &&
           DecideGozRecovery(false, false, false, GozServiceState::Unknown, false, false) ==
               GozRecoveryOutcome::ServiceMissing;
}

} // namespace
} // namespace goz_recovery
} // namespace miaodesk

int wmain() {
    using namespace miaodesk::goz_recovery;

    if (!VerdictStillMoves()) {
        std::printf("\n[FAIL] 反空洞自检没通过:恢复判定不动\n");
        return 1;
    }
    std::printf("  [PASS] 反空洞自检:服务停着且等到 → Recovered;SCM 打不开 → ServiceMissing\n");
    ++g_checks;

    // ---- 1. Win32 状态数值的映射 ----
    // 数值写错会让"停着"被读成"正在起",于是不该拉的也拉。逐个钉。
    {
        Check(DescribeGozServiceState(1) == GozServiceState::Stopped, "1 → Stopped");
        Check(DescribeGozServiceState(2) == GozServiceState::StartPending, "2 → StartPending");
        Check(DescribeGozServiceState(3) == GozServiceState::StopPending, "3 → StopPending");
        Check(DescribeGozServiceState(4) == GozServiceState::Running, "4 → Running");
        Check(DescribeGozServiceState(5) == GozServiceState::ContinuePending, "5 → ContinuePending");
        Check(DescribeGozServiceState(6) == GozServiceState::PausePending, "6 → PausePending");
        Check(DescribeGozServiceState(7) == GozServiceState::Paused, "7 → Paused");
        Check(DescribeGozServiceState(0) == GozServiceState::Unknown, "0 → Unknown");
        Check(DescribeGozServiceState(8) == GozServiceState::Unknown, "8 → Unknown");
        Check(DescribeGozServiceState(0xFFFFFFFFu) == GozServiceState::Unknown, "0xFFFFFFFF → Unknown");
    }

    // ---- 2. 管道本来就在:什么都不做,也不报 ----
    {
        const auto outcome = DecideGozRecovery(true, false, false, GozServiceState::Unknown, false, false);
        Check(outcome == GozRecoveryOutcome::NotNeeded, "管道本来通 → NotNeeded");
        Check(GozRecoverySucceeded(outcome), "NotNeeded 算成功");
        Check(!GozRecoveryNeedsNotice(outcome), "NotNeeded 不必跟用户说(没问就报是噪音)");
        Check(ExplainGozRecovery(outcome).empty(), "NotNeeded 没有要说的话");
    }

    // ---- 3. 服务侧的问题:每一种都要单独说得出口 ----
    {
        // SCM 打不开,或服务不在 —— 这是安装/权限,不是"再等等"。
        Check(DecideGozRecovery(false, false, true, GozServiceState::Stopped, true, false) ==
                  GozRecoveryOutcome::ServiceMissing,
              "SCM 打不开 → ServiceMissing(不是 TimedOut)");
        Check(DecideGozRecovery(false, true, false, GozServiceState::Stopped, true, false) ==
                  GozRecoveryOutcome::ServiceMissing,
              "服务句柄打不开 → ServiceMissing");
        Check(DecideGozRecovery(false, false, false, GozServiceState::Unknown, false, false) ==
                  GozRecoveryOutcome::ServiceMissing,
              "SCM 与服务都打不开 → ServiceMissing");
        Check(GozRecoveryNeedsNotice(GozRecoveryOutcome::ServiceMissing), "ServiceMissing 要说");

        // 状态查不到:不猜,也不乱拉。
        Check(DecideGozRecovery(false, true, true, GozServiceState::Unknown, false, false) ==
                  GozRecoveryOutcome::StatusUnknown,
              "状态查不到 → StatusUnknown");
        Check(GozRecoveryNeedsNotice(GozRecoveryOutcome::StatusUnknown), "StatusUnknown 要说");

        // 被禁用/拒绝。
        Check(DecideGozRecovery(false, true, true, GozServiceState::Stopped, false, false) ==
                  GozRecoveryOutcome::StartRefused,
              "服务停着但 StartService 没叫出去 → StartRefused");
        Check(DecideGozRecovery(false, true, true, GozServiceState::Paused, true, false) ==
                  GozRecoveryOutcome::StartRefused,
              "暂停中的服务 → StartRefused(那是 ResumeService 的活,不是 StartService)");
    }

    // ---- 4. 核心:不许对已经在起的服务再叫一次 StartService ----
    // 对 START_PENDING 叫 StartService 会拿到 ERROR_SERVICE_ALREADY_RUNNING,
    // 而调用方一律忽略返回值 —— 于是"其实已经在起"被记成"启动被拒绝",
    // 用户看到的是一个假的失败原因。
    {
        Check(DecideGozRecovery(false, true, true, GozServiceState::StartPending, true, true) ==
                  GozRecoveryOutcome::AlreadyStarting,
              "服务正在自己起且等到了 → AlreadyStarting,不要再叫 StartService");
        Check(!GozRecoveryNeedsNotice(GozRecoveryOutcome::AlreadyStarting),
              "AlreadyStarting 不必报(它自己会好)");
        // 等服务起来之后,这一轮就是成功的。
        Check(DecideGozRecovery(false, true, true, GozServiceState::StartPending, true, true) ==
                  GozRecoveryOutcome::AlreadyStarting,
              "等到了也算 AlreadyStarting(不需要额外动作)");
        Check(GozRecoverySucceeded(GozRecoveryOutcome::AlreadyStarting),
              "AlreadyStarting 算成功(它自己在起,而且等到了)");
        // 一直在起但等不满 → TimedOut,不是 AlreadyStarting。
        Check(DecideGozRecovery(false, true, true, GozServiceState::StartPending, true, false) ==
                  GozRecoveryOutcome::TimedOut,
              "一直在起但等不满 → TimedOut(第一版这里无条件返 AlreadyStarting,与 Succeeded 矛盾)");
        // STOP_PENDING:叫不动,也等不到。
        Check(DecideGozRecovery(false, true, true, GozServiceState::StopPending, true, false) ==
                  GozRecoveryOutcome::TimedOut,
              "服务正在停 → TimedOut(叫不动,只能等)");
    }

    // ---- 5. 服务说在跑但管道不通 ----
    // 这不是启动问题。等一等,但不该报"启动失败"。
    {
        Check(DecideGozRecovery(false, true, true, GozServiceState::Running, false, false) ==
                  GozRecoveryOutcome::TimedOut,
              "服务在跑但管道不通 → TimedOut(不是 StartRefused:没拉过)");
        Check(DecideGozRecovery(false, true, true, GozServiceState::Running, false, true) ==
                  GozRecoveryOutcome::Recovered,
              "服务在跑且管道后来通了 → Recovered");
    }

    // ---- 6. Stopped 的四条路 ----
    {
        Check(Recovered() == GozRecoveryOutcome::Recovered, "停着 + 拉了 + 等到 → Recovered");
        Check(GozRecoverySucceeded(GozRecoveryOutcome::Recovered), "Recovered 算成功");
        Check(!GozRecoveryNeedsNotice(GozRecoveryOutcome::Recovered), "Recovered 不必报");
        Check(ExplainGozRecovery(GozRecoveryOutcome::Recovered).empty(), "Recovered 没有要说的话");

        Check(Recovered(false) == GozRecoveryOutcome::StartRefused,
              "停着 + 没拉 + 等到了 → StartRefused(等到了也不是拉起来的功劳)");
        Check(DecideGozRecovery(false, true, true, GozServiceState::Stopped, true, false) ==
                  GozRecoveryOutcome::TimedOut,
              "停着 + 拉了 + 没等到 → TimedOut");
        Check(DecideGozRecovery(false, true, true, GozServiceState::Stopped, false, true) ==
                  GozRecoveryOutcome::StartRefused,
              "停着 + 没拉 + 等到了 → StartRefused(管道通不是它拉的)");
    }

    // ---- 7. 九种结局全部可达,且各有各的说法 ----
    // 这一条防"某种观测组合永远走不到":一个不可达的分支与没有它长得一样。
    {
        const GozRecoveryOutcome seen[] = {
            GozRecoveryOutcome::NotNeeded, GozRecoveryOutcome::ServiceMissing,
            GozRecoveryOutcome::AccessDenied, GozRecoveryOutcome::StatusUnknown,
            GozRecoveryOutcome::StartIssued, GozRecoveryOutcome::AlreadyStarting,
            GozRecoveryOutcome::StartRefused, GozRecoveryOutcome::Recovered,
            GozRecoveryOutcome::TimedOut};
        std::wstring texts;
        for (auto o : seen) {
            const std::wstring text = ExplainGozRecovery(o);
            // 不说的一律是"没问题":NotNeeded / Recovered / StartIssued / AlreadyStarting。
            // 其余的必须留下一句,而且互不相同。
            if (GozRecoveryNeedsNotice(o)) {
                Check(!text.empty(), std::string("结局 ") + ToString(o) + " 有话说");
                Check(texts.find(text) == std::string::npos,
                      std::string("结局 ") + ToString(o) + " 的说法独一无二");
                texts += text;
            } else {
                Check(text.empty() || !GozRecoveryNeedsNotice(o),
                      std::string("结局 ") + ToString(o) + " 不要求说话");
            }
        }
        // 每一句都必须说清"应用搜索仍然可用"或等价意思 —— SEARCH-04 的验收原话
        // 就是"不丢 App Search",而用户看见的第一句话就是这句。
        const GozRecoveryOutcome noisy[] = {GozRecoveryOutcome::ServiceMissing,
                                            GozRecoveryOutcome::AccessDenied,
                                            GozRecoveryOutcome::StatusUnknown,
                                            GozRecoveryOutcome::StartRefused,
                                            GozRecoveryOutcome::TimedOut};
        for (auto o : noisy) {
            const std::wstring text = ExplainGozRecovery(o);
            Check(text.find(L"应用搜索仍然可用") != std::wstring::npos,
                  std::string("结局 ") + ToString(o) + " 必须说明应用搜索仍然可用");
        }
    }

    // ---- 8. AccessDenied 与 ServiceMissing 必须分开 ----
    // 上一版把两者都报成"没连上",而它们的下一步完全相反:
    // 一个要装服务,一个要提权。
    {
        const std::wstring missing = ExplainGozRecovery(GozRecoveryOutcome::ServiceMissing);
        const std::wstring denied = ExplainGozRecovery(GozRecoveryOutcome::AccessDenied);
        Check(missing.find(L"没有安装") != std::wstring::npos, "ServiceMissing 说要装");
        Check(denied.find(L"管理员") != std::wstring::npos, "AccessDenied 说要提权");
        Check(missing != denied, "两者不能是同一句话");
        Check(missing.find(L"gozd") != std::wstring::npos, "ServiceMissing 点名 gozd");
    }

    if (g_failures != 0) {
        std::printf("\n失败 %d / %d\n", g_failures, g_checks);
        return 1;
    }
    std::printf("\ngoz 恢复判定:全部 %d 项通过\n", g_checks);
    return 0;
}
