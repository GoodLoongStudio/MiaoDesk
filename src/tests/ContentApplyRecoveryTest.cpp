// CCA-11:应用事务的一次恢复。
//
// 这份测试的重点是**撤销的方向是破坏的,而它看起来总是最稳妥的那一个**。
// 一笔"已发出、没结论"的应用摆在面前时,把它当作没落地、写回前态,读起来像谨慎;
// 可一旦用户后来又应用了另一份,写回前态就把人家后来那份抹掉了 ——
// 而用户看到的是自己的设置莫名倒退,他不知道为什么。
//
// 所以四种现状各配一个只属于它的断言:
//   落地了   → FinishApply(撤销才是错的)
//   没落地   → RollBack(写回账本里那一份精确前态)
//   被接管   → AbandonSuperseded(一个字都不写)
//   读不出来 → GiveUpUnrecognized(不许按"没落地"猜)
//
// 另外三条各自对应一次真实的误判:
//   * 已经有结论的一笔不再恢复 —— 恢复本身在写桌面,第二次就是重复发生一次结果;
//   * 落地了也要先重验候选 —— 崩溃这段时间里候选可能已被宿主标记失效;
//   * 没有精确前态时**不**撤销 —— 取"最近用过的一项"会把用户一个无关的设置写回去。
//
// 还有一条验收"同路径新版本不会被误认为已应用"按摘要判:同路径换了内容就是新版本。
//
// 账本只有一部分由真正的 CreationWorkflow 走出来。另一半是手工摆的条目,
// 原因是状态机本身:一次成功应用之后阶段停在 Applied,于是用一台 workflow
// 摆不出"这个目标上已经挂着一份、而新的一笔正在飞"。判定函数要的正是这种局面,
// 所以那几节直接摆账本 —— 但摆出来的条目与 CreationWorkflow 记录的字段一一对应
// (见 HandMadeEntry),而能被 workflow 走出来的节都尽量走真的。
#include "miaodesk/ContentApplyRecovery.h"

#include "miaodesk/CreationWorkflow.h"

#include <cstdio>
#include <string>
#include <vector>

// ---- 摆账本 ----------------------------------------------------------------
// 字段与 CreationWorkflow 写进 ApplyIdempotencyEntry 的完全一致。

// ---- 下面这些走真的 CreationWorkflow ----------------------------------------
namespace miaodesk::creator {
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

void CheckEq(const std::string& actual, const std::string& expected, const std::string& what) {
    ++g_checks;
    if (actual != expected) {
        ++g_failures;
        std::printf("FAIL  %s\n        expected: %s\n        actual:   %s\n",
                    what.c_str(), expected.c_str(), actual.c_str());
    }
}

// 字段与 CreationWorkflow 写进 ApplyIdempotencyEntry 的完全一致。
ApplyIdempotencyEntry HandMadeEntry(const char* digest, const char* target,
                                    const char* operationId, bool committed, bool resolved,
                                    const char* beforeState) {
    ApplyIdempotencyEntry entry;
    entry.key.candidateDigest = digest;
    entry.key.target = target;
    entry.key.operationId = operationId;
    entry.committed = committed;
    entry.resolved = resolved;
    entry.result = committed ? "已写入" : "应用进行中";
    entry.beforeState = beforeState;
    return entry;
}

ApplyRecoveryRequest Ask(const ApplyIdempotencyEntry* entry, bool readable, const char* observed) {
    ApplyRecoveryRequest request;
    request.entry = entry;
    request.observedReadable = readable;
    request.observedCandidateDigest = observed;
    return request;
}

CreationMessage Msg(const CreationWorkflow& w, std::uint64_t turnId, const char* operationId = "") {
    CreationMessage message;
    message.sessionId = w.Session().sessionId;
    message.epoch = w.Session().epoch;
    message.turnId = turnId;
    message.operationId = operationId ? operationId : "";
    return message;
}

CreationSession MakeSession(std::string id) {
    CreationSession session;
    session.sessionId = std::move(id);
    session.stage = CreationStage::Draft;
    session.provider.visionVerified = true;
    return session;
}

// 推到 Ready 的完整会话。
CreationWorkflow MakeReady(std::string id = "S-1") {
    CreationWorkflow workflow(MakeSession(std::move(id)), CreationBudget{});
    workflow.Apply({.type = CreationEventType::BriefSubmitted, .message = Msg(workflow, 0)});
    workflow.Apply({.type = CreationEventType::PreparationSucceeded, .message = Msg(workflow, 1)});
    workflow.Apply({.type = CreationEventType::CandidateSubmitted, .message = Msg(workflow, 1),
                    .candidateId = "cand-1", .digest = "digest-1", .summary = "第一版候选"});
    workflow.Apply({.type = CreationEventType::ValidationSucceeded, .message = Msg(workflow, 1),
                    .candidateId = "cand-1", .digest = "digest-1", .summary = "第一版候选"});
    workflow.Apply({.type = CreationEventType::EvidenceCollected, .message = Msg(workflow, 1),
                    .candidateId = "cand-1", .digest = "digest-1"});
    workflow.Apply({.type = CreationEventType::ReviewCompleted, .message = Msg(workflow, 1),
                    .candidateId = "cand-1", .digest = "digest-1", .reviewNote = "无阻塞问题"});
    return workflow;
}

// 用户点击"应用到桌面":进 Applying,账本留下一笔"已发出、没有结论"的在途。
void BeginApply(CreationWorkflow& workflow, const char* operationId, const char* target,
                const char* beforeState) {
    workflow.Apply({.type = CreationEventType::ApplyRequested,
                    .message = Msg(workflow, 1, operationId),
                    .candidateId = "cand-1",
                    .digest = "digest-1",
                    .target = target,
                    .operationId = operationId,
                    .beforeState = beforeState});
}

// 正式 API 给结论。
void SettleApply(CreationWorkflow& workflow, bool committed, const char* operationId,
                 const char* target) {
    workflow.Apply({.type = committed ? CreationEventType::ApplyCommitted
                                      : CreationEventType::ApplyFailed,
                    .message = Msg(workflow, 1, operationId),
                    .candidateId = "cand-1",
                    .digest = "digest-1",
                    .detail = committed ? "已写入" : "写入失败,原状未改",
                    .target = target,
                    .operationId = operationId});}

// 账本里那一笔 operationId 指名的、未结清的在途。
const ApplyIdempotencyEntry* Inflight(const CreationWorkflow& workflow,
                                      const char* operationId) {
    for (const auto& entry : workflow.ApplyLedger()) {
        if (entry.key.operationId == operationId && !entry.resolved) return &entry;
    }
    return nullptr;
}

const ApplyIdempotencyEntry* Settled(const CreationWorkflow& workflow,
                                     const char* operationId) {
    for (const auto& entry : workflow.ApplyLedger()) {
        if (entry.key.operationId == operationId && entry.resolved) return &entry;
    }
    return nullptr;
}

} // namespace
} // namespace miaodesk::creator

int wmain() {
    using namespace miaodesk::creator;

    // --- 1. 落地了:撤销才是错的 -------------------------------------------
    {
        auto workflow = MakeReady();
        BeginApply(workflow, "op-1", "monitor-1", "before=aurora");
        const ApplyRecoveryPlan plan =
            PlanApplyRecovery(workflow.ApplyLedger(),
                              Ask(Inflight(workflow, "op-1"), true, "digest-1"));
        Check(plan.action == ApplyRecoveryAction::FinishApply, "已经落地的一笔 → 补结算");
        Check(plan.restoreState.empty(), "补结算不写任何东西");
        Check(plan.revalidateCandidateFirst, "且补结算之前必须先重验候选");
        Check(plan.reason.find("落地") != std::string::npos, "原因说明它已经落地了");
    }

    // --- 2. 没落地:写回精确前态 -------------------------------------------
    {
        // 这个目标上此前挂着 digest-0(op-0 已结算成功),现在 op-1 正在飞。
        std::vector<ApplyIdempotencyEntry> ledger = {
            HandMadeEntry("digest-0", "monitor-1", "op-0", /*committed=*/true, /*resolved=*/true,
                          "before=none"),
            HandMadeEntry("digest-1", "monitor-1", "op-1", /*committed=*/false,
                          /*resolved=*/false, "before=aurora"),
        };
        const ApplyRecoveryPlan plan =
            PlanApplyRecovery(ledger, Ask(&ledger[1], true, "digest-0"));
        Check(plan.action == ApplyRecoveryAction::RollBack, "没落地的一笔 → 撤销");
        CheckEq(plan.restoreState, "before=aurora", "撤销写回的是账本记下的精确前态");
        Check(!plan.revalidateCandidateFirst, "撤销不需要重验候选 —— 它什么都没改变");
        CheckEq(PreviousCandidateDigestForTarget(ledger, "monitor-1", "op-1"), "digest-0",
                "前一本账来自账本自己,不是宿主另传一份");
        // 注意"digest-0"与"before=aurora"是两回事:判"没落地"比的是摘要,
        // 写回去的却是那份精确前态原文。混成同一个值会让这条永远测不到。
        Check(plan.restoreState.find("digest-0") == std::string::npos,
              "撤销写回的确实是前态原文,不是那个摘要");
    }

    // 此前一个候选都没有:前态就是 kNoAppliedCandidate。
    {
        auto workflow = MakeReady();
        BeginApply(workflow, "op-1", "monitor-1", "none");
        const ApplyRecoveryPlan plan =
            PlanApplyRecovery(workflow.ApplyLedger(),
                              Ask(Inflight(workflow, "op-1"), true, "none"));
        Check(plan.action == ApplyRecoveryAction::RollBack, "此前没有候选且没落地 → 撤销");
        CheckEq(plan.restoreState, "none", "前态 'none' 也被原样写回");
    }

    // 宿主读到"目标上没有候选"时报空串和报 "none" 必须是同一件事 ——
    // 否则同一份盘上状态会得出两个相反的恢复动作,而哪一个全看宿主那一次怎么填。
    {
        auto workflow = MakeReady();
        BeginApply(workflow, "op-1", "monitor-1", "none");
        const ApplyRecoveryPlan plan =
            PlanApplyRecovery(workflow.ApplyLedger(), Ask(Inflight(workflow, "op-1"), true, ""));
        Check(plan.action == ApplyRecoveryAction::RollBack, "空串与 'none' 结论一致");
        CheckEq(plan.restoreState, "none", "且前态一致");
    }

    // --- 3. 被接管:一个字都不写 -------------------------------------------
    {
        // 目标上原本挂着 digest-0(op-0 已结算),op-1 飞到一半进程没了,
        // 这期间用户又应用了 digest-2(op-2 已结算)。op-1 至今没有结论。
        std::vector<ApplyIdempotencyEntry> ledger = {
            HandMadeEntry("digest-0", "monitor-1", "op-0", true, true, "before=none"),
            HandMadeEntry("digest-1", "monitor-1", "op-1", false, false, "before=digest-0"),
            HandMadeEntry("digest-2", "monitor-1", "op-2", true, true, "before=digest-1"),
        };
        const ApplyRecoveryPlan plan = PlanApplyRecovery(ledger, Ask(&ledger[1], true, "digest-2"));
        Check(plan.action == ApplyRecoveryAction::AbandonSuperseded, "被接管的一笔 → 作废");
        Check(plan.restoreState.empty(), "且一个字都不写");
        Check(plan.reason.find("抹掉") != std::string::npos,
              "原因点明写回前态会抹掉用户后来的成果");
        // 而"现在挂的是哪一份"必须说的是用户后来那一份,不是这一笔的。
        CheckEq(CurrentCandidateDigestForTarget(ledger, "monitor-1"), "digest-2",
                "当前生效的是用户后来那一份");
        // op-1 没落地过,所以它的前一本账仍然是 digest-0 —— 不是它自己。
        CheckEq(PreviousCandidateDigestForTarget(ledger, "monitor-1", "op-1"), "digest-0",
                "没落地的那一笔不算进前一本账");
        CheckEq(PreviousCandidateDigestForTarget(ledger, "monitor-1", "op-2"), "digest-0",
                "op-2 的前一本账也是 digest-0 —— op-1 从未生效");
    }

    // --- 4. 读不出来:不许按"没落地"猜 -------------------------------------
    {
        auto workflow = MakeReady();
        BeginApply(workflow, "op-1", "monitor-1", "before=aurora");
        const ApplyRecoveryPlan plan =
            PlanApplyRecovery(workflow.ApplyLedger(),
                              Ask(Inflight(workflow, "op-1"), false, "digest-1"));
        Check(plan.action == ApplyRecoveryAction::GiveUpUnrecognized, "读不到目标现状 → 停下");
        Check(plan.restoreState.empty(), "且不写任何东西");
        Check(plan.reason.find("猜") != std::string::npos, "原因说明这是在猜用户桌面");
    }

    // 读到了,但目标上什么都没有,而账本说之前有过一份:中间发生过不相关的事。
    {
        std::vector<ApplyIdempotencyEntry> ledger = {
            HandMadeEntry("digest-1", "monitor-1", "op-0", true, true, "before=none"),
            HandMadeEntry("digest-2", "monitor-1", "op-1", false, false, "before=digest-1"),
        };
        const ApplyRecoveryPlan plan = PlanApplyRecovery(ledger, Ask(&ledger[1], true, ""));
        Check(plan.action == ApplyRecoveryAction::GiveUpUnrecognized, "目标上的候选消失了 → 停下");
        Check(plan.restoreState.empty(), "仍然一个字都不写");
        Check(plan.reason.find("digest-1") != std::string::npos, "原因带上账本记下的前态");
    }

    // --- 5. 已经有结论的一笔不再恢复 ---------------------------------------
    {
        auto workflow = MakeReady();
        BeginApply(workflow, "op-1", "monitor-1", "before=aurora");
        SettleApply(workflow, /*committed=*/true, "op-1", "monitor-1");
        const ApplyRecoveryPlan plan =
            PlanApplyRecovery(workflow.ApplyLedger(),
                              Ask(Settled(workflow, "op-1"), true, "digest-1"));
        Check(plan.action == ApplyRecoveryAction::NothingToRecover, "已结算的一笔不再恢复");
        Check(plan.restoreState.empty(), "不写任何东西");

        // 失败同样是结论。失败没有改变桌面,但"再恢复一次"会第二次发出应用。
        auto failed = MakeReady();
        BeginApply(failed, "op-1", "monitor-1", "before=aurora");
        SettleApply(failed, /*committed=*/false, "op-1", "monitor-1");
        const ApplyRecoveryPlan failedPlan =
            PlanApplyRecovery(failed.ApplyLedger(),
                              Ask(Settled(failed, "op-1"), true, "digest-1"));
        Check(failedPlan.action == ApplyRecoveryAction::NothingToRecover,
              "已给出失败结论的一笔也不再恢复");
    }

    // 没有这一笔:同样没有要恢复的东西。
    {
        const auto workflow = MakeReady();
        const ApplyRecoveryPlan plan =
            PlanApplyRecovery(workflow.ApplyLedger(), Ask(nullptr, true, "digest-1"));
        Check(plan.action == ApplyRecoveryAction::NothingToRecover, "账本里没有这一笔 → 无事可做");
        CheckEq(std::string(ToString(ApplyRecoveryAction::NothingToRecover)), "NothingToRecover",
                "空闲动作名稳定");
        CheckEq(std::string(ToString(ApplyRecoveryAction::FinishApply)), "FinishApply", "落地动作名");
        CheckEq(std::string(ToString(ApplyRecoveryAction::RollBack)), "RollBack", "撤销动作名");
        CheckEq(std::string(ToString(ApplyRecoveryAction::AbandonSuperseded)), "AbandonSuperseded",
                "作废动作名");
        CheckEq(std::string(ToString(ApplyRecoveryAction::GiveUpUnrecognized)),
                "GiveUpUnrecognized", "停下动作名");
    }

    // --- 6. 没有精确前态时不撤销 -------------------------------------------
    {
        // 空 beforeState 只在"本来该撤销"的时候才走得到 —— 所以这里的现状必须正好等于前态。
        auto workflow = MakeReady();
        BeginApply(workflow, "op-1", "monitor-1", "");
        const ApplyRecoveryPlan plan =
            PlanApplyRecovery(workflow.ApplyLedger(),
                              Ask(Inflight(workflow, "op-1"), true, "none"));
        Check(plan.action == ApplyRecoveryAction::GiveUpUnrecognized, "没有精确前态 → 不撤销");
        Check(plan.restoreState.empty(), "一个字都不写");
        Check(plan.reason.find("精确前态") != std::string::npos, "原因点明缺精确前态");
    }

    // 一笔连目标或摘要都没有的"在途"也恢复不了。
    {
        std::vector<ApplyIdempotencyEntry> ledger = {
            HandMadeEntry("", "monitor-1", "op-1", false, false, "before=aurora"),
        };
        const ApplyRecoveryPlan plan = PlanApplyRecovery(ledger, Ask(&ledger[0], true, "digest-1"));
        Check(plan.action == ApplyRecoveryAction::GiveUpUnrecognized, "缺候选摘要的在途恢复不了");
        Check(plan.restoreState.empty(), "且不写任何东西");
    }

    // --- 7. 同路径新版本不会被误认为已应用 ---------------------------------
    {
        auto workflow = MakeReady();
        BeginApply(workflow, "op-1", "monitor-1", "before=none");
        SettleApply(workflow, /*committed=*/true, "op-1", "monitor-1");
        Check(IsAlreadyAppliedOnTarget(workflow.ApplyLedger(), "monitor-1", "digest-1"),
              "同一份摘要就是已应用");
        // 同路径换了内容 = 新版本。摘要不同,所以它**不是**已应用。
        Check(!IsAlreadyAppliedOnTarget(workflow.ApplyLedger(), "monitor-1", "digest-2"),
              "同路径的新版本不算已应用 —— 判据是摘要,不是路径");
        Check(!IsAlreadyAppliedOnTarget(workflow.ApplyLedger(), "monitor-1", ""),
              "空摘要一律不算已应用");
        Check(!IsAlreadyAppliedOnTarget(workflow.ApplyLedger(), "monitor-2", "digest-1"),
              "别的目标上挂的也不算这个目标已应用");
        // 失败的应用没有改变桌面,所以它不算"已应用"。
        auto failed = MakeReady();
        BeginApply(failed, "op-1", "monitor-1", "before=aurora");
        SettleApply(failed, /*committed=*/false, "op-1", "monitor-1");
        Check(!IsAlreadyAppliedOnTarget(failed.ApplyLedger(), "monitor-1", "digest-1"),
              "应用失败的目标不算已应用");
    }

    // --- 8. 在途的那一笔自己不算"已生效",两个目标互不干扰 -----------------
    {
        auto workflow = MakeReady();
        BeginApply(workflow, "op-1", "monitor-1", "before=none");
        BeginApply(workflow, "op-w", "widget-slot-1", "none");
        // 在途期间问"现在挂的是哪一份":答案是空,不是这一笔的摘要 ——
        // 否则恢复会把一笔从未落地的东西判成"落地了"。
        CheckEq(CurrentCandidateDigestForTarget(workflow.ApplyLedger(), "monitor-1"), "",
                "在途的一笔不算当前生效");
        // 目标上什么都没有时,问"空摘要是不是已应用"必须是否。
        // 去掉 IsAlreadyAppliedOnTarget 里那个空摘要的前置判断,这里会因为
        // "两边都是空"而变成是 —— 而那等于说"空摘要已经应用成功了"。
        Check(!IsAlreadyAppliedOnTarget(workflow.ApplyLedger(), "monitor-1", ""),
              "目标上没有候选时空摘要也不算已应用");
        CheckEq(CurrentCandidateDigestForTarget(workflow.ApplyLedger(), "widget-slot-1"), "",
                "另一个目标上的在途也不算当前生效");
        CheckEq(PreviousCandidateDigestForTarget(workflow.ApplyLedger(), "monitor-1", "op-1"),
                std::string(kNoAppliedCandidate), "此前没有候选时前态是 'none'");
        CheckEq(PreviousCandidateDigestForTarget(workflow.ApplyLedger(), "monitor-2", "op-1"),
                std::string(kNoAppliedCandidate), "别的目标上问前态也是 'none'");
    }

    std::printf("\nCCA-11 apply recovery: %d checks, %d failures\n", g_checks, g_failures);
    if (g_failures) {
        std::printf("ALL CHECKS FAILED\n");
        return 1;
    }
    std::printf("ALL CHECKS PASSED\n");
    return 0;
}
