#include "miaodesk/ContentApplyRecovery.h"

#include <string>

namespace miaodesk::creator {

const char* ToString(ApplyRecoveryAction action) noexcept {
    switch (action) {
    case ApplyRecoveryAction::NothingToRecover: return "NothingToRecover";
    case ApplyRecoveryAction::FinishApply: return "FinishApply";
    case ApplyRecoveryAction::RollBack: return "RollBack";
    case ApplyRecoveryAction::AbandonSuperseded: return "AbandonSuperseded";
    case ApplyRecoveryAction::GiveUpUnrecognized: return "GiveUpUnrecognized";
    }
    return "Unknown";
}

namespace {

// 空摘要读作 kNoAppliedCandidate。宿主读到"目标上没有候选"时报告空串和报告 "none"
// 必须是同一件事 —— 否则同一份盘上状态会得出两个相反的恢复动作,而哪一种全看
// 宿主那一次怎么填的。
std::string Normalize(std::string digest) {
    return digest.empty() ? std::string(kNoAppliedCandidate) : digest;
}

bool CountsAsApplied(const ApplyIdempotencyEntry& entry) noexcept {
    // 两者都要:已提交但还没结论的在途(实际不会出现,但账本允许)不算生效;
    // 结论是失败的也不算 —— 失败没有改变桌面。
    return entry.resolved && entry.committed;
}

} // namespace

std::string CurrentCandidateDigestForTarget(const std::vector<ApplyIdempotencyEntry>& ledger,
                                            std::string_view target) {
    if (target.empty()) return {};
    std::string current;
    for (const auto& entry : ledger) {
        if (entry.key.target != target) continue;
        if (!CountsAsApplied(entry)) continue;
        current = entry.key.candidateDigest;
    }
    return current;
}

std::string PreviousCandidateDigestForTarget(const std::vector<ApplyIdempotencyEntry>& ledger,
                                             std::string_view target,
                                             std::string_view operationId) {
    if (target.empty() || operationId.empty()) return std::string(kNoAppliedCandidate);
    std::string previous(kNoAppliedCandidate);
    for (const auto& entry : ledger) {
        if (entry.key.target != target) continue;
        // 只看这一笔**之前**的。把它自己也算进去,会把"没落地"误判成"落地了"。
        if (entry.key.operationId == operationId) break;
        if (!CountsAsApplied(entry)) continue;
        previous = entry.key.candidateDigest;
    }
    return previous;
}

bool IsAlreadyAppliedOnTarget(const std::vector<ApplyIdempotencyEntry>& ledger,
                             std::string_view target, std::string_view candidateDigest) {
    if (target.empty() || candidateDigest.empty()) return false;
    return CurrentCandidateDigestForTarget(ledger, target) == candidateDigest;
}

ApplyRecoveryPlan PlanApplyRecovery(const std::vector<ApplyIdempotencyEntry>& ledger,
                                    const ApplyRecoveryRequest& request) {
    ApplyRecoveryPlan plan;
    const ApplyIdempotencyEntry* entry = request.entry;

    if (entry == nullptr) {
        plan.reason = "账本里没有这一笔,没有什么要恢复的。";
        return plan;
    }
    // 已经拿到正式结论的一笔不再恢复。恢复本身是往用户桌面上写,
    // 对一笔已结算的操作再写一次,就是把"已经发生的结果"又发生了一遍。
    if (entry->resolved || entry->committed) {
        plan.reason = "这一笔已经有正式结论了,不再恢复 —— 恢复会改用户桌面,而它已经结算过。";
        return plan;
    }
    if (entry->key.target.empty() || entry->key.candidateDigest.empty()) {
        // 一笔没有目标或摘要的"在途"恢复不了:连要恢复的是哪一次应用都说不出来。
        plan.action = ApplyRecoveryAction::GiveUpUnrecognized;
        plan.reason = "这一笔缺目标或候选摘要,无法恢复;它本来就不该被记进账本。";
        return plan;
    }
    if (!request.observedReadable) {
        // 见文件头:读不到就不能按"没落地"处理,那是对用户桌面的猜测,
        // 而猜错的方向恰好是破坏性的。
        plan.action = ApplyRecoveryAction::GiveUpUnrecognized;
        plan.reason = "宿主读不到目标当前挂的是哪一份候选,不能恢复 —— "
                      "按'没落地'处理是对用户桌面的猜测,而猜错会覆盖他后来装的东西。";
        return plan;
    }

    const std::string observed = Normalize(request.observedCandidateDigest);
    const std::string previous =
        PreviousCandidateDigestForTarget(ledger, entry->key.target, entry->key.operationId);

    // 1. 落地了。撤销才是错的。
    if (observed == entry->key.candidateDigest) {
        plan.action = ApplyRecoveryAction::FinishApply;
        plan.revalidateCandidateFirst = true;
        plan.reason = "这一笔其实已经落地了(目标上挂的就是它的候选)。补一次结算,"
                      "不要撤销一个已经生效的东西;结算之前先重验候选。";
        return plan;
    }

    // 2. 没落地:目标上还是这一笔之前的值。
    if (observed == previous) {
        if (entry->beforeState.empty()) {
            // 精确前态必须真的被记下来。取"最近用过的一项"会把用户一个无关的设置
            // 写回去 —— 那正是这次恢复最不该做的事。
            plan.action = ApplyRecoveryAction::GiveUpUnrecognized;
            plan.reason = "这一笔落地了没有无法从现状判断,而账本里没有精确前态 —— "
                          "撤销需要一个确定的前值,不能取'最近用过的一项'。";
            return plan;
        }
        plan.action = ApplyRecoveryAction::RollBack;
        plan.restoreState = entry->beforeState;
        plan.reason = "目标上还是应用之前的值,这一笔什么都没发生;写回账本记下的精确前态。";
        return plan;
    }

    // 3. 目标上**什么都没有**了,而账本说之前有过一份。中间发生过不相关的事,
    //    这时候写任何东西都是猜。
    if (observed == kNoAppliedCandidate) {
        plan.action = ApplyRecoveryAction::GiveUpUnrecognized;
        plan.reason = "目标上现在没有候选,而账本记的前态是 " + previous +
                      " —— 中间发生过别的事,恢复不猜该写什么。";
        return plan;
    }

    // 4. 剩下的一种:目标上挂着另一份候选。用户后来又应用了别的一份。
    plan.action = ApplyRecoveryAction::AbandonSuperseded;
    plan.reason = "目标上挂着另一份候选(" + observed +
                  "),而这一笔要应用的是 " + entry->key.candidateDigest +
                  "。这一笔作废,一个字都不写 —— 写回前态会把用户后来的成果一起抹掉。";
    return plan;
}

} // namespace miaodesk::creator
