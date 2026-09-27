#include "miaodesk/CreationWorkflow.h"

#include <algorithm>
#include <sstream>
#include <utility>

namespace miaodesk::creator {
namespace {

// 取消后仍要结算的事件:已经开始的应用事务必须凭正式 API 的真实结果收尾,
// 不能只把消息丢掉。其它事件在 Cancelling/Cancelled 之后一律拒绝。
bool SettlesAfterCancel(CreationEventType type) noexcept {
    return type == CreationEventType::ApplyCommitted || type == CreationEventType::ApplyFailed;
}

// 用户意图不是"迟到回调"。它们要过的关是归属和取消,不是轮次新旧 ——
// 否则用户在第二轮补充需求时,会因为 turnId 停在上一轮而被无声拒绝,
// 而那是用户刚刚亲手做的事。
bool IsUserIntent(CreationEventType type) noexcept {
    switch (type) {
    case CreationEventType::BriefSubmitted:
    case CreationEventType::BriefInsufficient:
    case CreationEventType::BriefRevised:
    case CreationEventType::ApplyRequested:
    case CreationEventType::CancelRequested:
    case CreationEventType::QueueDrained:
        return true;
    default:
        return false;
    }
}

const char* ToolName(CreatorTool tool) noexcept {
    switch (tool) {
    case CreatorTool::CapabilitiesGet: return "creator_capabilities_get";
    case CreatorTool::SkillGet: return "content_skill_get";
    case CreatorTool::PackageRead: return "creator_package_read";
    case CreatorTool::PackageUpdate: return "creator_package_update";
    case CreatorTool::AssetImport: return "creator_asset_import";
    case CreatorTool::ImageGenerate: return "creator_image_generate";
    case CreatorTool::CandidateSubmit: return "creator_candidate_submit";
    case CreatorTool::PreviewEvidence: return "creator_preview_evidence";
    }
    return "unknown";
}

// 会改候选内容的工具,只允许在制作与修复的那几个阶段出现。
// 只读工具放得更宽:Ready 之后用户仍要能看自己拿到的东西,
// 而"能看"不等于"能改"——把 PackageUpdate 也放到 Ready 是这次修的另一半。
bool IsMutatingTool(CreatorTool tool) noexcept {
    switch (tool) {
    case CreatorTool::PackageUpdate:
    case CreatorTool::AssetImport:
    case CreatorTool::ImageGenerate:
    case CreatorTool::CandidateSubmit:
        return true;
    default:
        return false;
    }
}

bool ToolAllowedInStage(CreatorTool tool, CreationStage stage) noexcept {
    if (stage == CreationStage::Draft || stage == CreationStage::NeedsInput) {
        // 还没冻结 brief:能查能力和读规则,不能碰候选。
        return !IsMutatingTool(tool) && tool != CreatorTool::PreviewEvidence;
    }
    if (stage == CreationStage::Preparing || stage == CreationStage::Generating ||
        stage == CreationStage::Validating || stage == CreationStage::Repairing ||
        stage == CreationStage::Rendering || stage == CreationStage::Reviewing) {
        return true;
    }
    if (stage == CreationStage::Ready) {
        // 已经就绪:读、看、再采一份证据都可以;改内容必须开新一轮。
        return !IsMutatingTool(tool);
    }
    // Applying / Applied / Cancelling / Cancelled / Failed / Queued 都不再动候选。
    return false;
}

} // namespace

const char* ToString(CreationStage stage) noexcept {
    switch (stage) {
    case CreationStage::Draft: return "Draft";
    case CreationStage::NeedsInput: return "NeedsInput";
    case CreationStage::Preparing: return "Preparing";
    case CreationStage::Generating: return "Generating";
    case CreationStage::Validating: return "Validating";
    case CreationStage::Rendering: return "Rendering";
    case CreationStage::Reviewing: return "Reviewing";
    case CreationStage::Repairing: return "Repairing";
    case CreationStage::Ready: return "Ready";
    case CreationStage::Applying: return "Applying";
    case CreationStage::Applied: return "Applied";
    case CreationStage::Cancelling: return "Cancelling";
    case CreationStage::Cancelled: return "Cancelled";
    case CreationStage::Failed: return "Failed";
    case CreationStage::Queued: return "Queued";
    }
    return "Unknown";
}

std::string ApplyIdempotencyKey::Encode() const {
    std::ostringstream out;
    out << candidateDigest << '|' << target << '|' << operationId;
    return out.str();
}

CreationWorkflow::CreationWorkflow(CreationSession session, CreationBudget budget)
    : session_(std::move(session)), budget_(budget) {}

bool CreationWorkflow::Terminal() const noexcept {
    return session_.stage == CreationStage::Applied || session_.stage == CreationStage::Cancelled ||
           session_.stage == CreationStage::Failed;
}

void CreationWorkflow::Enter(CreationStage stage) {
    session_.stage = stage;
    if (draftPersist_) {
        const auto* candidate = LastValidCandidate();
        draftPersist_(session_, candidate ? *candidate : CandidateRevision{});
    }
}

std::vector<CreationEffect> CreationWorkflow::Notify(std::string detail) const {
    CreationEffect effect;
    effect.type = CreationEffectType::Notify;
    effect.sessionId = session_.sessionId;
    effect.epoch = session_.epoch;
    effect.turnId = session_.turnId;
    effect.detail = std::move(detail);
    return {effect};
}

bool CreationWorkflow::Accepts(const CreationMessage& message, const CreationEvent& event) {
    if (Terminal() && !SettlesAfterCancel(event.type)) return false;
    if (message.sessionId != session_.sessionId) {
        lastRejection_ = "消息 sessionId 与当前作品不符";
        return false;
    }
    // 停止请求之后 epoch 已递增,旧 epoch 的回调一律不得再改状态。
    if (message.epoch != session_.epoch) {
        lastRejection_ = "消息 epoch 已过期(取消或切换作品后的迟到回调)";
        return false;
    }
    if (session_.cancelRequested && !SettlesAfterCancel(event.type)) {
        lastRejection_ = "已请求停止,不再接受新的制作步骤";
        return false;
    }
    if (!IsUserIntent(event.type) && message.turnId < session_.turnId) {
        lastRejection_ = "消息来自上一轮";
        return false;
    }
    return true;
}

std::vector<CreationEffect> CreationWorkflow::Apply(const CreationEvent& event) {
    if (!Accepts(event.message, event)) return {};

    std::vector<CreationEffect> effects;
    switch (event.type) {
    case CreationEventType::BriefSubmitted:
        if (session_.stage != CreationStage::Draft && session_.stage != CreationStage::NeedsInput &&
            session_.stage != CreationStage::Queued) {
            break;
        }
        Enter(CreationStage::Preparing);
        effects.push_back({CreationEffectType::FreezeBriefAndCapabilities, session_.sessionId,
                           session_.epoch, session_.turnId, {}, {}, {}, {}, "冻结 brief 与运行能力", false});
        break;

    case CreationEventType::BriefInsufficient:
        Enter(CreationStage::NeedsInput);
        effects.push_back({CreationEffectType::Notify, session_.sessionId, session_.epoch,
                           session_.turnId, {}, {}, {}, {},
                           event.detail.empty() ? std::string("还缺少必要信息") : event.detail, false});
        break;

    case CreationEventType::PreparationSucceeded:
        if (session_.stage != CreationStage::Preparing) break;
        ++session_.turnId;
        budget_.startedAtMs = budget_.nowMs;
        budget_.repairRoundsUsed = 0;
        budget_.visualReviewsUsed = 0;
        budget_.evidenceFramesUsed = 0;
        Enter(CreationStage::Generating);
        effects.push_back({CreationEffectType::OpenWorkspace, session_.sessionId, session_.epoch,
                           session_.turnId, {}, {}, {}, {}, "打开该作品专属工作区", false});
        effects.push_back({CreationEffectType::StartGenerationTurn, session_.sessionId,
                           session_.epoch, session_.turnId, {}, {}, {}, {}, "开始生成轮次", false});
        break;

    case CreationEventType::PreparationFailed:
        Enter(CreationStage::Failed);
        effects.push_back({CreationEffectType::RecordFailure, session_.sessionId, session_.epoch,
                           session_.turnId, {}, {}, {}, {},
                           event.detail.empty() ? "准备工作失败" : event.detail, false});
        break;

    case CreationEventType::CandidateSubmitted:
        if (session_.stage != CreationStage::Generating && session_.stage != CreationStage::Repairing) break;
        // 模型交出的路径/ID/成功声明只按输入处理:candidateId 由宿主重新分配,
        // 摘要由宿主对封存快照计算,所以这里只用它携带的定位信息。
        pendingCandidateId_ = event.candidateId;
        Enter(CreationStage::Validating);
        effects.push_back({CreationEffectType::SnapshotCandidate, session_.sessionId, session_.epoch,
                           session_.turnId, event.candidateId, event.digest, {}, {},
                           event.summary, false});
        effects.push_back({CreationEffectType::ValidateCandidate, session_.sessionId, session_.epoch,
                           session_.turnId, event.candidateId, event.digest, {}, {}, event.summary, false});
        break;

    case CreationEventType::ValidationSucceeded: {
        if (session_.stage != CreationStage::Validating) break;
        CandidateRevision revision;
        revision.candidateId = pendingCandidateId_.empty() ? std::string("cand-") +
            std::to_string(candidates_.size() + 1) : pendingCandidateId_;
        revision.revision = static_cast<std::uint32_t>(candidates_.size() + 1);
        revision.digest = event.digest;
        revision.summary = event.summary.empty() ? revision.candidateId : event.summary;
        revision.validated = true;
        revision.validationError.clear();
        revision.sourceTurn = session_.turnId;
        candidates_.push_back(revision);
        Enter(CreationStage::Rendering);
        effects.push_back({CreationEffectType::CollectEvidence, session_.sessionId, session_.epoch,
                           session_.turnId, revision.candidateId, revision.digest, {}, {},
                           "采集候选实际效果", budget_.EvidenceExhausted()});
        break;
    }

    case CreationEventType::ValidationFailed: {
        if (session_.stage != CreationStage::Validating) break;
        auto* candidate = FindCandidate(pendingCandidateId_);
        if (candidate) {
            candidate->validated = false;
            candidate->validationError = event.errorCode.empty() ? event.detail : event.errorCode;
            if (!event.errorLocation.empty()) {
                candidate->validationError += " @ ";
                candidate->validationError += event.errorLocation;
            }
        }
        const bool repairable = event.errorCode != "unrepairable" &&
                                event.errorCode != "service_unavailable" &&
                                event.errorCode != "kind_mismatch";
        if (repairable && !budget_.RepairExhausted() && !budget_.WallClockExhausted()) {
            ++budget_.repairRoundsUsed;
            Enter(CreationStage::Repairing);
            effects.push_back({CreationEffectType::RequestRepair, session_.sessionId, session_.epoch,
                               session_.turnId, pendingCandidateId_, event.digest, {}, {},
                               event.detail, budget_.RepairExhausted()});
        } else {
            // 保留上一有效候选,说明原因和下一动作。
            Enter(CreationStage::Failed);
            effects.push_back({CreationEffectType::RecordFailure, session_.sessionId, session_.epoch,
                               session_.turnId, pendingCandidateId_, event.digest, {}, {},
                               budget_.RepairExhausted() ? "自动修复轮次已用尽" : "校验失败且不可自动修复",
                               true});
        }
        break;
    }

    case CreationEventType::EvidenceCollected: {
        if (session_.stage != CreationStage::Rendering) break;
        auto* candidate = FindCandidate(pendingCandidateId_);
        if (candidate) candidate->evidenceCollected = true;
        Enter(CreationStage::Reviewing);
        // 视觉评审只在"该 Profile 已确证支持视觉"时才排。否则如实标记未审,
        // 不把一次无声降级记成一次已完成的评审(见 CCA-01 的探测结论)。
        const bool canReview = session_.provider.visionVerified && !budget_.VisualReviewExhausted();
        effects.push_back({CreationEffectType::RunVisualReview, session_.sessionId, session_.epoch,
                           session_.turnId, pendingCandidateId_, event.digest, {}, {},
                           canReview ? "执行视觉评审" : "视觉评审未执行", !canReview});
        break;
    }

    case CreationEventType::EvidenceFailed: {
        if (session_.stage != CreationStage::Rendering) break;
        auto* candidate = FindCandidate(pendingCandidateId_);
        if (candidate) candidate->evidenceCollected = false;
        // 缺素材或渲染失败绝不能用封面图替代成功。
        Enter(CreationStage::Failed);
        effects.push_back({CreationEffectType::RecordFailure, session_.sessionId, session_.epoch,
                           session_.turnId, pendingCandidateId_, event.digest, {}, {},
                           event.detail.empty() ? "渲染证据采集失败" : event.detail, true});
        break;
    }

    case CreationEventType::ReviewCompleted: {
        if (session_.stage != CreationStage::Reviewing) break;
        auto* candidate = FindCandidate(pendingCandidateId_);
        if (candidate) {
            candidate->visualReviewed = event.reviewVerdict != CreationReviewVerdict::VisualNotReviewed;
            candidate->visualReviewSkipped = event.reviewVerdict == CreationReviewVerdict::VisualNotReviewed;
            candidate->visualReviewNote = event.reviewNote;
        }
        if (event.reviewVerdict == CreationReviewVerdict::Blocking) {
            if (!budget_.RepairExhausted() && !budget_.WallClockExhausted()) {
                ++budget_.repairRoundsUsed;
                Enter(CreationStage::Repairing);
                effects.push_back({CreationEffectType::RequestRepair, session_.sessionId,
                                   session_.epoch, session_.turnId, pendingCandidateId_,
                                   event.digest, {}, {}, event.reviewNote, budget_.RepairExhausted()});
                break;
            }
            // 预算耗尽:保留最佳有效候选,展示未解决项,不谎称已修。
            Enter(CreationStage::Ready);
            effects.push_back({CreationEffectType::PresentReady, session_.sessionId, session_.epoch,
                               session_.turnId, pendingCandidateId_, event.digest, {}, {},
                               "有未解决的评审问题,先展示当前最佳候选", true});
            break;
        }
        Enter(CreationStage::Ready);
        effects.push_back({CreationEffectType::PresentReady, session_.sessionId, session_.epoch,
                           session_.turnId, pendingCandidateId_, event.digest, {}, {},
                           event.reviewNote, false});
        break;
    }

    case CreationEventType::RepairAttempted:
        if (session_.stage != CreationStage::Repairing) break;
        Enter(CreationStage::Validating);
        effects.push_back({CreationEffectType::ValidateCandidate, session_.sessionId, session_.epoch,
                           session_.turnId, pendingCandidateId_, event.digest, {}, {},
                           event.detail, false});
        break;

    case CreationEventType::RepairExhausted:
        Enter(CreationStage::Ready);
        effects.push_back({CreationEffectType::PresentReady, session_.sessionId, session_.epoch,
                           session_.turnId, pendingCandidateId_, event.digest, {}, {},
                           event.detail.empty() ? "修复预算已用尽" : event.detail, true});
        break;

    case CreationEventType::ApplyRequested:
        // 唯一的 Applying 入口:用户明确点击应用。模型文本不可能驱动到这里。
        if (session_.stage != CreationStage::Ready) {
            // 不静默 break。用户点了一个按钮而什么都没发生,却查不到原因,
            // 是"按钮丢了点击"那一类缺陷;这里至少要把阶段说清楚。
            lastRejection_ = std::string("当前阶段 ") + ToString(session_.stage) +
                             " 不是 Ready,不能应用";
            break;
        }
        if (event.operationId.empty() || event.target.empty() || event.digest.empty()) {
            lastRejection_ = "应用请求缺少目标或候选摘要";
            return Notify("应用请求不完整,已拒绝");
        }
        if (!ApplyPreconditionHolds({event.digest, event.target, event.operationId})) {
            lastRejection_ = "应用前重验未通过(候选或目标已失效)";
            return Notify("候选或目标已变化,请重新选择后再应用");
        }
        Enter(CreationStage::Applying);
        // 立刻入账(未提交)。否则双击的第二次点在 ApplyPreconditionHolds 眼里
        // 仍然是"没做过",两次 BeginApply 都会发出去 —— 幂等就只保护了回调,
        // 没保护用户的第二次点击。
        RecordApplyOutcome({event.digest, event.target, event.operationId}, false,
                           /*resolved=*/false, "应用进行中", event.beforeState, 0);
        effects.push_back({CreationEffectType::BeginApply, session_.sessionId, session_.epoch,
                           session_.turnId, pendingCandidateId_, event.digest, event.target,
                           event.operationId, event.beforeState, false});
        break;

    case CreationEventType::ApplyCommitted:
    case CreationEventType::ApplyFailed: {
        const bool committed = event.type == CreationEventType::ApplyCommitted;
        if (event.operationId.empty() || event.digest.empty() || event.target.empty()) {
            // 既不能假装成功,也不能悄悄丢掉:这一笔不结算,让宿主重发完整的结论。
            lastRejection_ = "应用结算缺少 operationId/digest/target,无法入账";
            return Notify("应用结果无法入账,需要重新确认。请查看日志或重试。");
        }
        const ApplyIdempotencyKey key{event.digest, event.target, event.operationId};
        // 结算要能到达:停在 Applying 正常,停在 Ready 是失败后的重复回调,
        // 停在 Cancelling 才是计划 4.3 要求的那一种 —— 应用已经开始,取消之后
        // 正式 API 才回。只要这一笔确实在飞,就按它的真实结果收尾。
        if (session_.stage != CreationStage::Applying && !IsApplyInFlight(key)) {
            // 已经有结论的同一笔:这是重复回调。不能什么都不返回 —— 宿主无法区分
            // "这是重复"和"我漏发了",于是会重试,而重试可能真的再改一次桌面。
            const ApplyIdempotencyEntry* existing = nullptr;
            for (const auto& entry : applyLedger_) {
                if (entry.key == key) existing = &entry;
            }
            if (!existing) break;
            effects.push_back({existing->committed ? CreationEffectType::ConfirmApply
                                                   : CreationEffectType::PresentReady,
                               session_.sessionId, session_.epoch, session_.turnId,
                               pendingCandidateId_, event.digest, event.target,
                               event.operationId, "重复确认,结果不变", false});
            break;
        }
        const bool firstSettle = RecordApplyOutcome(key, committed, /*resolved=*/true,
                                                   event.detail, event.beforeState,
                                                   budget_.nowMs);
        if (firstSettle) {
            // 这一笔已经结清了,不再算"取消在途"—— 否则它会继续挡住后续合法事件,
            // 而用户看到的却是桌面已经改好了。
            session_.cancelRequested = false;
            Enter(committed ? CreationStage::Applied : CreationStage::Ready);
        }
        effects.push_back({committed ? CreationEffectType::ConfirmApply
                                     : CreationEffectType::PresentReady,
                           session_.sessionId, session_.epoch, session_.turnId, pendingCandidateId_,
                           event.digest, event.target, event.operationId,
                           !firstSettle ? "重复确认,结果不变"
                                        : (event.detail.empty()
                                               ? (committed ? "已应用" : "应用失败,原状态未改")
                                               : event.detail),
                           false});
        break;
    }

    case CreationEventType::CancelRequested:
        if (Terminal()) break;
        ++session_.epoch;
        session_.cancelRequested = true;
        Enter(CreationStage::Cancelling);
        effects.push_back({CreationEffectType::ReleaseResources, session_.sessionId, session_.epoch,
                           session_.turnId, {}, {}, {}, {}, "停止新步骤并释放本次运行资源", false});
        break;

    case CreationEventType::CancelSettled:
        if (session_.stage != CreationStage::Cancelling && session_.stage != CreationStage::Cancelled) break;
        Enter(CreationStage::Cancelled);
        effects.push_back({CreationEffectType::Notify, session_.sessionId, session_.epoch,
                           session_.turnId, {}, {}, {}, {},
                           "已停止。已经开始执行的操作不会被撤销。", false});
        break;

    case CreationEventType::ServiceError:
        if (!Accepts(event.message, event)) break;
        Enter(CreationStage::Failed);
        effects.push_back({CreationEffectType::RecordFailure, session_.sessionId, session_.epoch,
                           session_.turnId, pendingCandidateId_, event.digest, {}, {},
                           event.detail.empty() ? "服务错误" : event.detail, true});
        break;

    case CreationEventType::BriefRevised:
        // 冻结之后改需求 = 新 brief revision,不能原地改掉正在制作的这一份。
        ++session_.briefRevision;
        session_.cancelRequested = false;
        Enter(CreationStage::Draft);
        effects.push_back({CreationEffectType::Notify, session_.sessionId, session_.epoch,
                           session_.turnId, {}, {}, {}, {},
                           "需求已更新为新的 brief revision,重新冻结后再生成", false});
        break;

    case CreationEventType::QueueDrained:
        if (session_.stage != CreationStage::Queued) break;
        Enter(CreationStage::Draft);
        effects.push_back({CreationEffectType::Notify, session_.sessionId, session_.epoch,
                           session_.turnId, {}, {}, {}, {}, "排队结束,可以开始制作", false});
        break;
    }

    return effects;
}

void CreationWorkflow::RequestCancel() {
    if (Terminal()) return;
    ++session_.epoch;
    session_.cancelRequested = true;
    session_.stage = CreationStage::Cancelling;
}

bool CreationWorkflow::RecordApplyOutcome(const ApplyIdempotencyKey& key, bool committed,
                                         bool resolved, std::string result,
                                         std::string beforeState, std::uint64_t completedAtMs) {
    for (auto& entry : applyLedger_) {
        if (entry.key == key) {
            // BeginApply 已经建过这一笔(resolved=false)。"第一次给出结论"指的是
            // resolved 从 false 翻到 true 的那一次,与成功/失败无关:失败同样是结论。
            // 重复回调和重复 begin 都不改结论 —— 第一次的真实结果说了算。
            const bool newlyResolved = resolved && !entry.resolved;
            if (newlyResolved) {
                entry.resolved = true;
                entry.committed = entry.committed || committed;
                entry.result = std::move(result);
                entry.beforeState = beforeState;
                entry.record.resolved = true;
                entry.record.committed = entry.committed;
                entry.record.result = entry.result;
                entry.record.beforeState = entry.beforeState;
                entry.record.completedAtMs = completedAtMs;
            }
            return newlyResolved;
        }
    }
    ApplyIdempotencyEntry entry;
    entry.key = key;
    entry.committed = committed;
    entry.resolved = resolved;
    entry.result = std::move(result);
    entry.beforeState = std::move(beforeState);
    entry.record.operationId = key.operationId;
    entry.record.sessionId = session_.sessionId;
    entry.record.turnId = session_.turnId;
    entry.record.candidateDigest = key.candidateDigest;
    entry.record.target = key.target;
    entry.record.started = true;
    entry.record.resolved = true;
    entry.record.committed = committed;
    entry.record.beforeState = entry.beforeState;
    entry.record.result = entry.result;
    entry.record.completedAtMs = completedAtMs;
    applyLedger_.push_back(std::move(entry));
    return true;
}

bool CreationWorkflow::ApplyPreconditionHolds(const ApplyIdempotencyKey& key) const {
    if (key.candidateDigest.empty() || key.target.empty() || key.operationId.empty()) return false;
    const auto* candidate = LastValidCandidate();
    if (!candidate || !candidate->validated) return false;
    if (candidate->digest != key.candidateDigest) return false;
    for (const auto& entry : applyLedger_) {
        // 已经成功过的同一操作不再执行。正在进行的同一操作不算 ——
        // 宿主在 BeginApply 之后、正式 API 之前也要再验一次,而那一刻账本里已经有
        // 这一笔的"进行中"记录;把它当成"做过"会把真正的应用挡掉。
        if (entry.key == key && entry.committed) return false;
    }
    return true;
}

bool CreationWorkflow::IsApplyInFlight(const ApplyIdempotencyKey& key) const {
    for (const auto& entry : applyLedger_) {
        if (entry.key == key) return !entry.resolved;
    }
    return false;
}

CandidateRevision* CreationWorkflow::FindCandidate(const std::string& candidateId) {
    if (candidateId.empty()) return nullptr;
    for (auto& candidate : candidates_) {
        if (candidate.candidateId == candidateId) return &candidate;
    }
    return nullptr;
}

const CandidateRevision* CreationWorkflow::FindCandidate(const std::string& candidateId) const {
    if (candidateId.empty()) return nullptr;
    for (const auto& candidate : candidates_) {
        if (candidate.candidateId == candidateId) return &candidate;
    }
    return nullptr;
}

// 最后那个通过校验的候选。取消、失败都不动它 —— 用户要能在"本轮失败"之后
// 仍然指着上一版说"就用这个"。
//
// 唯一事实来源是 candidates_ 本身。曾经在 CreationSession 里还存了一份
// lastValidCandidateId,于是"上一有效候选"有两个来源;清掉其中一份时另一份仍然
//  Green,看起来像没有行为变化 —— 那种同时存在的双源正是要被消掉的。
const CandidateRevision* CreationWorkflow::LastValidCandidate() const noexcept {
    const CandidateRevision* best = nullptr;
    for (const auto& candidate : candidates_) {
        if (candidate.validated) best = &candidate;
    }
    return best;
}

CreatorToolOutcome CreationWorkflow::AuthorizeToolCall(const CreatorToolRequest& request) const {
    CreatorToolOutcome outcome;
    if (request.context.sessionId != session_.sessionId) {
        outcome.error = {"session_mismatch", false, "context.sessionId",
                         "工具调用的 sessionId 不属于当前作品"};
        return outcome;
    }
    if (request.context.epoch != session_.epoch) {
        outcome.error = {"epoch_stale", false, "context.epoch", "工具调用来自已取消或已切换的作品"};
        return outcome;
    }
    if (session_.cancelRequested) {
        outcome.error = {"cancelled", false, "context.sessionId", "作品已请求停止,不再接受工具调用"};
        return outcome;
    }
    if (request.context.workspaceRoot.empty()) {
        outcome.error = {"workspace_missing", false, "context.workspaceRoot", "缺少该作品的工作区"};
        return outcome;
    }
    if (!ToolAllowedInStage(request.tool, session_.stage)) {
        outcome.error = {"stage_not_allowed", false, "context.sessionId",
                         std::string("当前阶段 ") + ToString(session_.stage) +
                             " 不允许 " + ToolName(request.tool)};
        return outcome;
    }
    outcome.success = true;
    outcome.payloadJson = "{}";
    return outcome;
}

} // namespace miaodesk::creator
