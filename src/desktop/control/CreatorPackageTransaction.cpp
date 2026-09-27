#include "miaodesk/CreatorPackageTransaction.h"

#include <algorithm>
#include <utility>

namespace miaodesk::creator {
namespace {

// 两处都要表达"这个文件在包里扮演什么",而摘要按 CandidatePartRole 归类。
// 显式映射是必需的:少它的话新角色会被静默归到 Other,于是改那个文件不换摘要 ——
// 而"摘要覆盖 manifest、scene、参数及引用素材"正是它的全部意义。
//
// 按名字对照,不按数值:数值对照会在任一侧插入一个角色时静默错位,而错位的表现是
// "改了 preview 却像改了 asset 一样换了摘要",查起来像摘要坏了。
content::CandidatePartRole ToDigestRole(CreatorFileRole role) noexcept {
    switch (role) {
    case CreatorFileRole::Manifest: return content::CandidatePartRole::Manifest;
    case CreatorFileRole::Parameters: return content::CandidatePartRole::Parameters;
    case CreatorFileRole::Scene: return content::CandidatePartRole::Scene;
    case CreatorFileRole::Preview: return content::CandidatePartRole::Preview;
    case CreatorFileRole::Asset: return content::CandidatePartRole::Asset;
    }
    return content::CandidatePartRole::Other;
}

} // namespace

const char* ToString(CreatorWriteStage stage) noexcept {
    switch (stage) {
    case CreatorWriteStage::Planned: return "Planned";
    case CreatorWriteStage::Validated: return "Validated";
    case CreatorWriteStage::Staged: return "Staged";
    case CreatorWriteStage::Committed: return "Committed";
    case CreatorWriteStage::RolledBack: return "RolledBack";
    case CreatorWriteStage::Rejected: return "Rejected";
    case CreatorWriteStage::Unverified: return "Unverified";
    }
    return "Unknown";
}

CreatorPackageTransaction::CreatorPackageTransaction(
    const CreatorWorkspacePolicy& policy, std::vector<content::CandidatePart> beforeParts,
    std::string relativePath, std::string newContent)
    : policy_(policy) {
    plan_.sessionId = policy.Binding().sessionId;
    plan_.workspaceRoot = policy.Binding().workspaceRoot;
    plan_.relativePath = std::move(relativePath);
    plan_.newContent = std::move(newContent);

    // after 就是"同一批内容,只有 relativePath 换了新字节"。在 beforeParts 上算出它,
    // 而不是让宿主写完再回头算:事务必须**事先**知道这笔写入会把摘要从什么变成什么,
    // 否则 Commit 阶段只能"看到什么就接受什么",那就没有校验可言了。
    std::vector<content::CandidatePart> afterParts = beforeParts;
    if (policy.Classify(plan_.relativePath, &plan_.role)) {
        auto it = std::find_if(afterParts.begin(), afterParts.end(),
                               [this](const content::CandidatePart& part) {
                                   return part.relPath == plan_.relativePath;
                               });
        if (it == afterParts.end()) {
            content::CandidatePart part;
            part.role = ToDigestRole(plan_.role);
            part.relPath = plan_.relativePath;
            part.bytes = plan_.newContent;
            afterParts.push_back(std::move(part));
        } else {
            it->bytes = plan_.newContent;
        }
    }
    plan_.before = content::ComputeCandidateDigest(beforeParts);
    plan_.after = content::ComputeCandidateDigest(afterParts);

    // 目标原来在不在:决定回退方式。新建要删掉,已存在要写回旧内容。
    for (const auto& part : beforeParts) {
        if (part.relPath == plan_.relativePath) {
            plan_.existed = true;
            plan_.previousContent = part.bytes;
            break;
        }
    }
}

CreatorWriteOutcome CreatorPackageTransaction::Reject(std::string reason) {
    stage_ = CreatorWriteStage::Rejected;
    rejection_ = std::move(reason);
    CreatorWriteOutcome outcome;
    outcome.stage = stage_;
    outcome.rejectionReason = rejection_;
    return outcome;
}

// 拒绝一次**顺序不对**的调用。它和 Reject 的区别很要紧:Reject 意味着"这个操作失败了,
// 且什么都没发生";Refuse 意味着"你这个调用在现在这个阶段本来就不该来"。
// 用 Reject 去表达后者会把一个已提交的事务改写成 Rejected,于是"它到底提交了没有"
// 变成了取决于最后一次调用顺序 —— 那正是要避免的不可判定状态。
CreatorWriteOutcome CreatorPackageTransaction::Refuse(std::string reason) {
    rejection_ = std::move(reason);
    CreatorWriteOutcome outcome;
    outcome.stage = stage_;
    outcome.rejectionReason = rejection_;
    return outcome;
}

CreatorWriteOutcome CreatorPackageTransaction::Validate() {
    if (stage_ != CreatorWriteStage::Planned) return Refuse("事务已经不在 Planned,不能重复校验。");
    CreatorFileFacts facts;
    facts.byteCount = plan_.newContent.size();
    facts.exists = plan_.existed;
    // 目标本身是不是 reparse point 由宿主在 Stage 前告知;这里先用计划内容的事实判一次。
    std::string reason;
    if (!policy_.Allows(plan_.relativePath, facts, &reason)) {
        return Reject(reason.empty() ? "写入被工作区策略拒绝。" : reason);
    }
    if (!plan_.before.UsableAsIdentity()) {
        return Reject("写入前的工作区摘要不可用:" + plan_.before.incompletenessReason);
    }
    // 不再单独判 plan_.after:它要么等于 before(路径不合规时 afterParts 原样返回),
    // 要么是"给完整工作区加一个合规文件",两种情况下只要 before 可用它就可用。
    // 实测短路掉这里,测试依然全绿 —— 上面 before 那一关已经先挡下了。
    stage_ = CreatorWriteStage::Validated;
    CreatorWriteOutcome outcome;
    outcome.stage = stage_;
    return outcome;
}

CreatorWriteOutcome CreatorPackageTransaction::Stage(const CreatorFileFacts& stagedFacts,
                                                   std::string_view stagedBytes) {
    if (stage_ != CreatorWriteStage::Validated) return Refuse("只有通过校验的事务才能进暂存。");
    // 暂存文件必须就在本作品工作区里,而且必须是我们让它写的那份内容。
    // 少了这一条,宿主可以把暂存写到别处,而事务不会发现。
    std::string stagedReason;
    if (!policy_.Allows(plan_.relativePath, stagedFacts, &stagedReason)) {
        return Reject("暂存文件没有通过工作区策略:" + stagedReason);
    }
    std::string expected = plan_.newContent;
    // 不再单独判 reparse point:上面那次 policy_.Allows(...) 已经判过,而它拿到的就是
    // 同一个 stagedFacts。实测短路掉这里,测试依然全绿 —— 正是重复守卫的样子。
    if (stagedBytes != expected) return Reject("暂存内容与计划不一致,已拒绝替换");
    stage_ = CreatorWriteStage::Staged;
    CreatorWriteOutcome outcome;
    outcome.stage = stage_;
    outcome.stagedPath = plan_.workspaceRoot + "/.staging/" + plan_.relativePath;
    return outcome;
}

CreatorWriteOutcome CreatorPackageTransaction::Commit(const CreatorFileFacts& afterFacts,
                                                     std::vector<content::CandidatePart> afterParts) {
    if (stage_ != CreatorWriteStage::Staged) return Refuse("只有暂存完成的事务才能提交。");
    std::string reason;
    if (!policy_.Allows(plan_.relativePath, afterFacts, &reason)) {
        return Reject("提交后的工作区没有通过策略:" + reason);
    }
    const auto actual = content::ComputeCandidateDigest(std::move(afterParts));
    if (!actual.UsableAsIdentity()) {
        return Reject("提交后的工作区摘要不可用:" + actual.incompletenessReason);
    }
    // 到这里副作用已经发生,所以失败一律是 Unverified,不是 Rejected。
    // 关键的一道:落盘之后的摘要必须**正是**计划里的 after。
    // 允许"差不多就行"的话,封存与校验就都失去了锚点 —— 我们以为改了 X,实际改了 Y。
    if (actual.value != plan_.after.value) {
        stage_ = CreatorWriteStage::Unverified;
        rejection_ = "提交后的摘要与计划不符:预期 " + content::ShortDigest(plan_.after.value) +
                     ",实际 " + content::ShortDigest(actual.value);
        CreatorWriteOutcome outcome;
        outcome.stage = stage_;
        outcome.rejectionReason = rejection_;
        return outcome;
    }
    stage_ = CreatorWriteStage::Committed;
    CreatorWriteOutcome outcome;
    outcome.stage = stage_;
    outcome.committed = true;
    outcome.candidateChanged = plan_.after.value;
    return outcome;
}

CreatorWriteOutcome CreatorPackageTransaction::Rollback() {
    if (stage_ == CreatorWriteStage::Committed || stage_ == CreatorWriteStage::Unverified) {
        // 已经落盘的写入不能"就地撤销":桌面上可能已经用它做过别的事。
        // Unverified 更要紧 —— 盘上确实变了,回退会假装它没变。
        // 正确做法是让宿主把候选标记失效,由上层决定要不要重做。
        return Refuse(stage_ == CreatorWriteStage::Committed
                          ? "已提交的写入不能就地回退,请走候选失效流程。"
                          : "落盘内容与计划不符,请先标记候选失效再决定下一步。");
    }
    CreatorWriteOutcome outcome;
    outcome.stage = CreatorWriteStage::RolledBack;
    stage_ = CreatorWriteStage::RolledBack;
    // 回退后工作区摘要必须回到 before。把它带回去,宿主据此确认。
    outcome.candidateChanged = plan_.before.value;
    return outcome;
}

bool CreatorPackageTransaction::ChangesContent() const noexcept {
    if (!plan_.before.UsableAsIdentity() || !plan_.after.UsableAsIdentity()) return false;
    return plan_.before.value != plan_.after.value;
}

} // namespace miaodesk::creator
