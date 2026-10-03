// CCA-02:创作工作流状态机的可执行测试。
//
// 覆盖 docs/CONTENT_CREATOR_AGENT_PLAN.md §8 验证矩阵的"纯逻辑"一行:
// 状态转换、预算、取消 epoch、旧消息、版本、幂等。
//
// 这个文件刻意不 include 任何 Windows 头,所以它既被 verify-windows-syntax.sh
// 交叉编译过,也被 run-pure-logic-tests.sh 真实编译并**运行**。
// 一个状态机如果只能靠 Windows 真机走查,那它等于没有测试。
//
// 事件一律用 designated initializer(.type = ... / .target = ...)。曾经用位置初始化,
// 结果把一个 beforeState 写到了 operationId 上,表现出来是"缺 operationId 被拒绝" ——
// 一个假失败,查起来像状态机坏了。按名字赋值之后这类错误不可能再发生。
#include "miaodesk/CreationWorkflow.h"

#include <cstdio>
#include <string>
#include <vector>

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

CreationMessage Msg(const CreationWorkflow& w, std::uint64_t turnId, const char* operationId = "") {
    CreationMessage message;
    message.sessionId = w.Session().sessionId;
    message.epoch = w.Session().epoch;
    message.turnId = turnId;
    message.operationId = operationId ? operationId : "";
    return message;
}

bool HasEffect(const std::vector<CreationEffect>& effects, CreationEffectType type) {
    for (const auto& effect : effects) {
        if (effect.type == type) return true;
    }
    return false;
}

CreationSession MakeSession(std::string id, bool visionVerified = true) {
    CreationSession session;
    session.sessionId = std::move(id);
    session.stage = CreationStage::Draft;
    session.provider.visionVerified = visionVerified;
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

// 用户点击"应用到桌面"的完整事件。
CreationEvent ApplyClick(const CreationWorkflow& workflow, const char* operationId = "op-1",
                        const char* target = "monitor-1") {
    return {.type = CreationEventType::ApplyRequested,
            .message = Msg(workflow, 1, operationId),
            .candidateId = "cand-1",
            .digest = "digest-1",
            .target = target,
            .operationId = operationId,
            .beforeState = "before=aurora"};
}

// 正式 API 的结论回调。
CreationEvent ApplyResult(const CreationWorkflow& workflow, bool committed,
                         const char* operationId = "op-1", const char* target = "monitor-1") {
    CreationEvent event;
    event.type = committed ? CreationEventType::ApplyCommitted : CreationEventType::ApplyFailed;
    event.message = Msg(workflow, 1, operationId);
    event.candidateId = "cand-1";
    event.digest = "digest-1";
    event.target = target;
    event.operationId = operationId;
    event.beforeState = "before=aurora";
    event.detail = committed ? "已写入" : "显示器已断开";
    return event;
}

// 一份绑定到指定作品的工具请求。sessionId/epoch 必须跟着作品走 ——
// 拿 A 作品的 context 去问 B 作品,当然是 session_mismatch,那不是被测行为。
CreatorToolRequest ToolCall(const CreationWorkflow& workflow, CreatorTool tool,
                            const char* workspaceRoot = R"(C:\work\S-1)") {
    CreatorToolRequest request;
    request.tool = tool;
    request.context.sessionId = workflow.Session().sessionId;
    request.context.epoch = workflow.Session().epoch;
    request.context.workspaceRoot = workspaceRoot;
    return request;
}

// ---------------------------------------------------------------------------

void TestHappyPathReachesReady() {
    auto workflow = MakeReady();
    Check(workflow.Stage() == CreationStage::Ready, "成功路径:生成->校验->渲染->评审->Ready");
    const auto* candidate = workflow.LastValidCandidate();
    Check(candidate != nullptr, "有一个有效候选");
    Check(candidate && candidate->validated, "候选已通过校验");
    Check(candidate && candidate->evidenceCollected, "候选已采集渲染证据");
    Check(candidate && candidate->visualReviewed, "评审已执行并记录");
    Check(candidate && !candidate->visualReviewSkipped, "没有把未审标成已审");
}

void TestModelTextCannotDriveApplied() {
    auto workflow = MakeReady();
    Check(workflow.Stage() == CreationStage::Ready, "起点是 Ready");
    // 没有用户操作 ID 的"应用"一律拒绝。这是模型文本唯一能触及的入口。
    const auto effects = workflow.Apply(ApplyClick(workflow, "", "monitor-1"));
    Check(workflow.Stage() != CreationStage::Applying, "缺 operationId 不能进入 Applying");
    Check(workflow.Stage() != CreationStage::Applied, "缺 operationId 更不能到 Applied");
    Check(HasEffect(effects, CreationEffectType::Notify), "拒绝时给了用户可理解的反馈");
}

// 上面那条从"没有候选"的状态起步;这一条从**有候选**的状态起步 —— 否则
// ApplyPreconditionHolds 会先因为"没有有效候选"把请求挡掉,阶段守卫根本轮不到它发挥作用,
// 于是"放宽阶段守卫"这种改动会被误判为无害。变异过:把守卫放宽到 Generating 时这条必须红。
//
// 走的是真实路径:MakeReady() 之后 BriefRevised。用户改了需求,上一版候选还在手里,
// 但此刻点"应用"必须被拒 —— 否则装上去的是用户刚刚改掉需求的旧版本。
void TestValidCandidateCannotBeAppliedOffReady() {
    for (int i = 0; i < 3; ++i) {
        auto workflow = MakeReady("S-have-candidate");
        Check(workflow.LastValidCandidate() != nullptr, "先有一版有效候选");
        workflow.Apply({.type = CreationEventType::BriefRevised,
                        .message = Msg(workflow, workflow.Session().turnId)});
        const auto stage = workflow.Stage();
        Check(stage != CreationStage::Ready, "改需求后不再停在 Ready");
        const auto effects = workflow.Apply(ApplyClick(workflow));
        Check(!HasEffect(effects, CreationEffectType::BeginApply),
              std::string("有候选但在 ") + ToString(stage) + " 阶段,点击应用不应发出 BeginApply");
        Check(workflow.ApplyLedger().empty(),
              std::string("有候选但在 ") + ToString(stage) + " 阶段,不应入账");
        Check(!workflow.LastRejectionReason().empty(),
              std::string("有候选但在 ") + ToString(stage) + " 阶段,拒绝要有原因");
    }
}

// 旧轮次的模型回调不得覆盖新轮次。这一条是 4.3 里"停止请求后增加 epoch,
// 旧回调不得覆盖新会话"的轮次那一半 —— epoch 挡的是取消/切换,turnId 挡的是
// 同一会话内新一轮开始后上一轮才回来的工具结果。变异过:关掉 turnId 门它必须红。
void TestOldTurnCallbackCannotOverwriteNewTurn() {
    auto workflow = MakeReady();
    // 用户改需求 → 新一轮开始(turnId 前进),上一轮的候选结果才回来。
    workflow.Apply({.type = CreationEventType::BriefRevised,
                    .message = Msg(workflow, workflow.Session().turnId)});
    workflow.Apply({.type = CreationEventType::BriefSubmitted,
                    .message = Msg(workflow, workflow.Session().turnId)});
    workflow.Apply({.type = CreationEventType::PreparationSucceeded,
                    .message = Msg(workflow, 2)});
    const auto turnAfterRestart = workflow.Session().turnId;
    Check(turnAfterRestart >= 2, "新一轮的 turnId 已经前进");

    CreationEvent stale;
    stale.type = CreationEventType::CandidateSubmitted;
    stale.message = Msg(workflow, 1);          // 上一轮
    stale.candidateId = "cand-old";
    stale.digest = "digest-old";
    stale.summary = "上一轮的产物";
    const auto before = workflow.Candidates().size();
    const auto effects = workflow.Apply(stale);
    Check(effects.empty(), "上一轮的候选提交被拒绝");
    Check(workflow.Stage() == CreationStage::Generating, "状态没有被拉回 Validating");
    Check(workflow.LastRejectionReason().find("上一轮") != std::string::npos,
          "且说明这是上一轮的消息");
    Check(workflow.Candidates().size() == before, "旧轮次的候选没有被登记进来");
}

// 重复的结算回调必须自己承认是重复,而不是再说一次"已应用"。
// 否则用户连着看到两条"已写入桌面",而其中一条什么也没做。
void TestDuplicateSettleSaysSo() {
    auto workflow = MakeReady();
    workflow.Apply(ApplyClick(workflow));
    const auto first = workflow.Apply(ApplyResult(workflow, /*committed=*/true));
    CheckEq(std::string(ToString(workflow.Stage())), "Applied", "第一次结算后是 Applied");

    const auto second = workflow.Apply(ApplyResult(workflow, /*committed=*/true));
    Check(workflow.Stage() == CreationStage::Applied, "重复回调不改变阶段");
    Check(workflow.ApplyLedger().size() == 1, "也不会产生第二笔账");
    Check(!second.empty() && second.back().detail == "重复确认,结果不变",
          R"(且明确告诉宿主这是重复确认,而不是再说一次「已写入」)");
    Check(!first.empty() && first.back().detail == "已写入", "第一次说的是已写入");
}
void TestCancelLeavesAReasonInsteadOfSilentlyDropping() {
    auto workflow = MakeReady();
    workflow.RequestCancel();
    CreationEvent intent;
    intent.type = CreationEventType::BriefSubmitted;
    intent.message = Msg(workflow, workflow.Session().turnId);
    const auto effects = workflow.Apply(intent);
    Check(effects.empty(), "取消后提交新 brief 不产生效果");
    Check(workflow.LastRejectionReason().find("停止") != std::string::npos,
          "且原因写明已请求停止(而不是被阶段守卫无声吞掉)");
}

// Applying 只有一个入口:用户在 Ready 阶段点的那一下。这条直接对应计划 4.2 的
// "Ready | 用户明确点击应用、版本和目标仍有效 | Applying"—— 任何其它路径都算
// 模型/回调伪造了用户意图。变异过:把守卫放宽到 Generating 时,这条必须变红。
void TestOnlyAUserClickAtReadyStartsAnApply() {
    for (auto stage : {CreationStage::Draft, CreationStage::NeedsInput,
                       CreationStage::Preparing, CreationStage::Generating,
                       CreationStage::Validating, CreationStage::Rendering,
                       CreationStage::Reviewing, CreationStage::Repairing,
                       CreationStage::Applying, CreationStage::Applied,
                       CreationStage::Failed, CreationStage::Cancelling,
                       CreationStage::Cancelled, CreationStage::Queued}) {
        CreationSession session = MakeSession("S-stage");
        session.stage = stage;
        CreationWorkflow workflow(session, CreationBudget{});
        if (stage == CreationStage::Ready) continue;   // 由 MakeReady 那条覆盖
        const auto before = workflow.Stage();
        const auto effects = workflow.Apply(ApplyClick(workflow));
        const std::string at = ToString(stage);
        Check(!HasEffect(effects, CreationEffectType::BeginApply),
              "在 " + at + " 阶段点击应用不应发出 BeginApply");
        Check(workflow.Stage() == before,
              "在 " + at + " 阶段点击应用不应改变阶段(仍是 " + ToString(before) + ")");
        Check(workflow.ApplyLedger().empty(),
              "在 " + at + " 阶段点击应用不应入账");
    }
}

void TestValidationFailureRetriesThenStops() {
    CreationWorkflow workflow(MakeSession("S-2"), CreationBudget{});
    workflow.Apply({.type = CreationEventType::BriefSubmitted, .message = Msg(workflow, 0)});
    workflow.Apply({.type = CreationEventType::PreparationSucceeded, .message = Msg(workflow, 1)});
    workflow.Apply({.type = CreationEventType::CandidateSubmitted, .message = Msg(workflow, 1),
                    .candidateId = "cand-1", .digest = "d", .summary = "s"});

    for (int i = 0; i < 2; ++i) {
        const auto repaired = workflow.Apply({.type = CreationEventType::ValidationFailed,
                                             .message = Msg(workflow, 1),
                                             .candidateId = "cand-1", .digest = "d",
                                             .errorCode = "param_below_min",
                                             .errorLocation = "scene.layers[0].opacity",
                                             .detail = "参数低于最小值"});
        Check(workflow.Stage() == CreationStage::Repairing,
              "第 " + std::to_string(i + 1) + " 次可修复失败后进入 Repairing(不要求用户重复点击)");
        Check(HasEffect(repaired, CreationEffectType::RequestRepair), "生成了携带定位信息的修复动作");
        workflow.Apply({.type = CreationEventType::RepairAttempted, .message = Msg(workflow, 1),
                        .candidateId = "cand-1", .digest = "d"});
    }

    const auto exhausted = workflow.Apply({.type = CreationEventType::ValidationFailed,
                                           .message = Msg(workflow, 1),
                                           .candidateId = "cand-1", .digest = "d",
                                           .errorCode = "param_below_min",
                                           .errorLocation = "scene.layers[0].opacity",
                                           .detail = "参数低于最小值"});
    Check(workflow.Stage() == CreationStage::Failed, "预算耗尽后停在 Failed");
    Check(HasEffect(exhausted, CreationEffectType::RecordFailure), "记录了失败原因");
    Check(workflow.Budget().RepairExhausted(), "修复预算已用尽");
    Check(workflow.Budget().repairRoundsMax == 2, "首版上限是 2 轮(计划 6.3)");
    Check(workflow.LastValidCandidate() == nullptr, "本轮从没成功过,所以没有上一版候选可留");
}

void TestPreviousCandidateSurvivesFailure() {
    auto workflow = MakeReady("S-2b");
    Check(workflow.LastValidCandidate() != nullptr, "先有一版成功候选");
    // 用户改需求后重新生成,新一版失败:上一版必须还在。
    workflow.Apply({.type = CreationEventType::BriefRevised, .message = Msg(workflow, 1)});
    workflow.Apply({.type = CreationEventType::BriefSubmitted, .message = Msg(workflow, 1)});
    workflow.Apply({.type = CreationEventType::PreparationSucceeded, .message = Msg(workflow, 2)});
    workflow.Apply({.type = CreationEventType::CandidateSubmitted, .message = Msg(workflow, 2),
                    .candidateId = "cand-2", .digest = "digest-2", .summary = "第二版"});
    workflow.Apply({.type = CreationEventType::ValidationFailed, .message = Msg(workflow, 2),
                    .candidateId = "cand-2", .digest = "digest-2",
                    .errorCode = "unrepairable", .errorLocation = "manifest.json",
                    .detail = "manifest 损坏"});
    Check(workflow.Stage() == CreationStage::Failed, "新版失败");
    const auto* kept = workflow.LastValidCandidate();
    Check(kept != nullptr, "但上一版候选没有被清掉");
    CheckEq(kept ? kept->digest : std::string(), "digest-1", "留下的是上一版的摘要");
}

// CREATE-04:新一轮失败不许把上一版可预览的结果带走。
//
// 上面那条 TestPreviousCandidateSurvivesFailure 是绿的,只因为它给第二轮用了
// 不同的 candidateId("cand-2")。一旦**复用**同一个 ID —— 而模型交出的 ID 是原样收下的
// (CandidateSubmitted 的注释写着"candidateId 由宿主重新分配",实际并没有)——
// ValidationFailed 的 FindCandidate(pendingCandidateId_) 命中的是上一轮那条**已经成功**
// 的记录,`validated = false` 落在它身上,LastValidCandidate() 就此空空。
//
// 用户在改需求重新生成之后指着上一版说"就用这个",而那正是 CREATE-04 的原话。
void TestReusedCandidateIdDoesNotTakeAwayTheLastGoodOne() {
    auto workflow = MakeReady("S-2c");
    const auto* before = workflow.LastValidCandidate();
    Check(before != nullptr, "先有一版成功候选");
    CheckEq(before ? before->digest : std::string(), "digest-1", "摘要就是它");

    // 用户改需求后重新生成,而模型交出的 candidateId 复用了上一轮的 "cand-1"。
    workflow.Apply({.type = CreationEventType::BriefRevised, .message = Msg(workflow, 1)});
    workflow.Apply({.type = CreationEventType::BriefSubmitted, .message = Msg(workflow, 1)});
    workflow.Apply({.type = CreationEventType::PreparationSucceeded, .message = Msg(workflow, 2)});
    workflow.Apply({.type = CreationEventType::CandidateSubmitted, .message = Msg(workflow, 2),
                    .candidateId = "cand-1", .digest = "digest-2", .summary = "第二版候选"});
    workflow.Apply({.type = CreationEventType::ValidationFailed, .message = Msg(workflow, 2),
                    .candidateId = "cand-1", .digest = "digest-2",
                    .errorCode = "unrepairable", .detail = "manifest 损坏"});

    Check(workflow.Stage() == CreationStage::Failed, "新版失败");
    const auto* kept = workflow.LastValidCandidate();
    Check(kept != nullptr, "★ 上一版候选没有被新一轮的失败带走(CREATE-04)");
    CheckEq(kept ? kept->digest : std::string(), "digest-1", "留下的仍是上一版的摘要");

    // 同一条 ID 复用,但**摘要相同**时要认 —— 同一份内容的重复提交不是另一轮。
    auto same = MakeReady("S-2d");
    same.Apply({.type = CreationEventType::BriefRevised, .message = Msg(same, 1)});
    same.Apply({.type = CreationEventType::BriefSubmitted, .message = Msg(same, 1)});
    same.Apply({.type = CreationEventType::PreparationSucceeded, .message = Msg(same, 2)});
    same.Apply({.type = CreationEventType::CandidateSubmitted, .message = Msg(same, 2),
                .candidateId = "cand-1", .digest = "digest-1", .summary = "同一份"});
    same.Apply({.type = CreationEventType::ValidationFailed, .message = Msg(same, 2),
                .candidateId = "cand-1", .digest = "digest-1",
                .errorCode = "unrepairable", .detail = "坏了"});
    Check(same.LastValidCandidate() == nullptr,
          "ID 与摘要都相同就是同一条记录:它失败时上一版确实失效");
}

void TestUnrepairableFailureDoesNotConsumeRepairBudget() {
    CreationWorkflow workflow(MakeSession("S-3"), CreationBudget{});
    workflow.Apply({.type = CreationEventType::BriefSubmitted, .message = Msg(workflow, 0)});
    workflow.Apply({.type = CreationEventType::PreparationSucceeded, .message = Msg(workflow, 1)});
    workflow.Apply({.type = CreationEventType::CandidateSubmitted, .message = Msg(workflow, 1),
                    .candidateId = "cand-1", .digest = "d", .summary = "s"});
    workflow.Apply({.type = CreationEventType::ValidationFailed, .message = Msg(workflow, 1),
                    .candidateId = "cand-1", .digest = "d",
                    .errorCode = "kind_mismatch", .detail = "包类型与模式不符"});
    Check(workflow.Stage() == CreationStage::Failed, "类型不符直接失败");
    Check(workflow.Budget().repairRoundsUsed == 0, "不可修复错误不消耗修复预算");
}

void TestCancelInvalidatesLateCallbacks() {
    auto workflow = MakeReady();
    Check(workflow.Stage() == CreationStage::Ready, "起点 Ready");
    workflow.RequestCancel();
    const auto epochAfterCancel = workflow.Session().epoch;
    Check(workflow.Stage() == CreationStage::Cancelling, "取消后进入 Cancelling");

    CreationEvent stale;
    stale.type = CreationEventType::EvidenceCollected;
    stale.message = Msg(workflow, 1);
    stale.message.epoch = epochAfterCancel - 1;
    stale.candidateId = "cand-1";
    stale.digest = "digest-1";
    const auto effects = workflow.Apply(stale);
    Check(effects.empty(), "取消后旧 epoch 的迟到回调不产生任何效果");
    Check(workflow.Stage() == CreationStage::Cancelling, "状态仍是 Cancelling");
    Check(workflow.LastRejectionReason().find("epoch") != std::string::npos,
          "拒绝原因是 epoch 过期");

    const auto after = workflow.Apply({.type = CreationEventType::EvidenceCollected,
                                       .message = Msg(workflow, 1),
                                       .candidateId = "cand-1", .digest = "digest-1"});
    Check(after.empty(), "已请求停止后不再启动新的制作步骤");
    Check(workflow.Session().cancelRequested, "取消标记保持");
}

void TestApplyStillSettlesAfterCancel() {
    auto workflow = MakeReady();
    // 真实顺序:用户点了应用(进 Applying,账本留下"进行中")→ 用户按了停止
    // → 正式 API 才返回。取消不得把已经发生的真实结果丢掉。
    workflow.Apply(ApplyClick(workflow));
    Check(workflow.Stage() == CreationStage::Applying, "先进入 Applying");
    workflow.RequestCancel();
    Check(workflow.Stage() == CreationStage::Cancelling, "然后取消");

    const auto effects = workflow.Apply(ApplyResult(workflow, /*committed=*/true));
    Check(!effects.empty(), "取消后已开始的提交仍被结算");
    CheckEq(std::string(ToString(workflow.Stage())), "Applied", "结算后是 Applied");
    Check(workflow.ApplyLedger().size() == 1, "账本里只有这一笔");
    Check(!workflow.ApplyLedger().empty() && workflow.ApplyLedger()[0].committed, "且记成已提交");
    Check(!workflow.ApplyLedger().empty() &&
              workflow.ApplyLedger()[0].record.beforeState == "before=aurora",
          "前态可用于恢复");
    Check(!workflow.Session().cancelRequested, "结算后不再当成取消在途");
}

void TestDoubleClickProducesOneApply() {
    auto workflow = MakeReady();
    const ApplyIdempotencyKey key{"digest-1", "monitor-1", "op-1"};
    const auto begin = workflow.Apply(ApplyClick(workflow));
    Check(HasEffect(begin, CreationEffectType::BeginApply), "第一次点击发出 BeginApply");

    // 第二次点击(同一个用户操作 ID,例如双击):阶段已经不在 Ready,直接无效。
    const auto second = workflow.Apply(ApplyClick(workflow));
    Check(!HasEffect(second, CreationEffectType::BeginApply), "第二次点击不再发出 BeginApply");
    Check(workflow.IsApplyInFlight(key), "且账本显示这一笔仍在进行中");
    Check(workflow.ApplyLedger().size() == 1, "不会产生第二笔");

    // 结算之后同一操作再点:被前重验挡住。
    workflow.Apply(ApplyResult(workflow, /*committed=*/true));
    Check(!workflow.IsApplyInFlight(key), "结算后不再是在途");
    Check(!workflow.ApplyPreconditionHolds(key), "同一操作不会被再次执行");
}

void TestIdempotentApply() {
    auto workflow = MakeReady();
    const ApplyIdempotencyKey key{"digest-1", "monitor-1", "op-1"};
    Check(workflow.ApplyPreconditionHolds(key), "应用前重验通过");
    Check(workflow.RecordApplyOutcome(key, true, /*resolved=*/true, "已写入", "before=aurora", 5000),
          "第一次结算被记录");
    Check(!workflow.RecordApplyOutcome(key, true, /*resolved=*/true, "又写了一次", "before=aurora",
                                      6000),
          "重复回调返回 false,不改写结果");
    Check(workflow.ApplyLedger().size() == 1, "账本只有一笔");
    CheckEq(workflow.ApplyLedger()[0].result, "已写入", "结果是第一次那一次");
    CheckEq(workflow.ApplyLedger()[0].record.beforeState, "before=aurora", "精确前态被记下");

    const ApplyIdempotencyKey second{"digest-1", "monitor-1", "op-2"};
    Check(workflow.ApplyPreconditionHolds(second), "新的用户操作 ID 允许再次执行");
    const ApplyIdempotencyKey changedTarget{"digest-1", "monitor-2", "op-1"};
    Check(workflow.ApplyPreconditionHolds(changedTarget), "目标变化也是新操作");
    const ApplyIdempotencyKey changedDigest{"digest-2", "monitor-1", "op-1"};
    Check(!workflow.ApplyPreconditionHolds(changedDigest),
          "候选摘要变化必须重新走应用,不能冒充已应用");
    Check(!workflow.ApplyPreconditionHolds({}), "空键一律拒绝");
}

void TestFailedApplyKeepsOriginalState() {
    auto workflow = MakeReady();
    workflow.Apply(ApplyClick(workflow));
    const auto effects = workflow.Apply(ApplyResult(workflow, /*committed=*/false));
    Check(workflow.Stage() == CreationStage::Ready, "应用失败回到 Ready,不是 Applied");
    Check(!workflow.ApplyLedger().empty() && !workflow.ApplyLedger()[0].committed,
          "账本记成未提交");
    Check(!workflow.ApplyLedger().empty() &&
              workflow.ApplyLedger()[0].result == "显示器已断开",
          "失败原因进了账本");
    Check(HasEffect(effects, CreationEffectType::PresentReady), "重新展示候选让用户再决定");
}

void TestBriefRevisionStartsOver() {
    CreationWorkflow workflow(MakeSession("S-4"), CreationBudget{});
    workflow.Apply({.type = CreationEventType::BriefSubmitted, .message = Msg(workflow, 0)});
    workflow.Apply({.type = CreationEventType::PreparationSucceeded, .message = Msg(workflow, 1)});
    workflow.Apply({.type = CreationEventType::CandidateSubmitted, .message = Msg(workflow, 1),
                    .candidateId = "cand-1", .digest = "d", .summary = "s"});

    const auto revised = workflow.Apply({.type = CreationEventType::BriefRevised,
                                         .message = Msg(workflow, 1)});
    Check(workflow.Session().briefRevision == 1, "改需求后 brief revision 被记下来");
    Check(workflow.Stage() == CreationStage::Draft, "回到 Draft,不原地改掉正在制作的 brief");
    Check(HasEffect(revised, CreationEffectType::Notify), "告诉用户要重新冻结");
}

void TestVisualReviewSkippedIsRecorded() {
    CreationWorkflow workflow(MakeSession("S-5", /*visionVerified=*/false), CreationBudget{});
    workflow.Apply({.type = CreationEventType::BriefSubmitted, .message = Msg(workflow, 0)});
    workflow.Apply({.type = CreationEventType::PreparationSucceeded, .message = Msg(workflow, 1)});
    workflow.Apply({.type = CreationEventType::CandidateSubmitted, .message = Msg(workflow, 1),
                    .candidateId = "cand-1", .digest = "digest-1", .summary = "s"});
    workflow.Apply({.type = CreationEventType::ValidationSucceeded, .message = Msg(workflow, 1),
                    .candidateId = "cand-1", .digest = "digest-1", .summary = "s"});
    const auto effects = workflow.Apply({.type = CreationEventType::EvidenceCollected,
                                         .message = Msg(workflow, 1),
                                         .candidateId = "cand-1", .digest = "digest-1"});
    Check(HasEffect(effects, CreationEffectType::RunVisualReview), "仍要走评审这一步");
    Check(!effects.empty() && effects.back().budgetExhausted,
          "但标记本轮没有视觉能力,不能当成已审");

    workflow.Apply({.type = CreationEventType::ReviewCompleted, .message = Msg(workflow, 1),
                    .candidateId = "cand-1", .digest = "digest-1",
                    .reviewVerdict = CreationReviewVerdict::VisualNotReviewed,
                    .reviewNote = "Provider 未确认支持视觉"});
    const auto* candidate = workflow.LastValidCandidate();
    Check(candidate && !candidate->visualReviewed, "候选没有记成已完成视觉评审");
    Check(candidate && candidate->visualReviewSkipped, "而是记成视觉未审");
    Check(candidate && candidate->visualReviewNote == "Provider 未确认支持视觉", "并留下原因");
}

void TestEvidenceFailureIsNotCoveredByStaticImage() {
    CreationWorkflow workflow(MakeSession("S-6"), CreationBudget{});
    workflow.Apply({.type = CreationEventType::BriefSubmitted, .message = Msg(workflow, 0)});
    workflow.Apply({.type = CreationEventType::PreparationSucceeded, .message = Msg(workflow, 1)});
    workflow.Apply({.type = CreationEventType::CandidateSubmitted, .message = Msg(workflow, 1),
                    .candidateId = "cand-1", .digest = "digest-1", .summary = "s"});
    workflow.Apply({.type = CreationEventType::ValidationSucceeded, .message = Msg(workflow, 1),
                    .candidateId = "cand-1", .digest = "digest-1", .summary = "s"});
    workflow.Apply({.type = CreationEventType::EvidenceFailed, .message = Msg(workflow, 1),
                    .candidateId = "cand-1", .digest = "digest-1", .detail = "缺素材 cloud.png"});
    Check(workflow.Stage() == CreationStage::Failed, "渲染证据失败后停在 Failed");
    const auto* candidate = workflow.LastValidCandidate();
    Check(candidate && !candidate->evidenceCollected, "且不记成已采集证据");
}

void TestWrongSessionRejected() {
    auto workflow = MakeReady();
    CreationEvent foreign;
    foreign.type = CreationEventType::EvidenceCollected;
    foreign.message = Msg(workflow, 1);
    foreign.message.sessionId = "S-other";
    foreign.candidateId = "cand-1";
    foreign.digest = "digest-1";
    Check(workflow.Apply(foreign).empty(), "别的作品的消息被拒绝");
    Check(workflow.Stage() == CreationStage::Ready, "且没有改变本作品状态");
    Check(workflow.LastRejectionReason().find("sessionId") != std::string::npos,
          "拒绝原因说明是 sessionId 不符");
}

void TestToolCallCannotForgeSession() {
    auto workflow = MakeReady();
    CreatorToolRequest forged;
    forged.tool = CreatorTool::PackageUpdate;
    forged.context.sessionId = "S-forged";
    forged.context.epoch = workflow.Session().epoch;
    forged.context.workspaceRoot = R"(C:\work\other)";
    const auto rejected = workflow.AuthorizeToolCall(forged);
    Check(!rejected.success, "伪造 sessionId 的工具调用被拒绝");
    CheckEq(rejected.error.code, "session_mismatch", "错误码可判定");

    const auto good = ToolCall(workflow, CreatorTool::PackageRead);
    Check(workflow.AuthorizeToolCall(good).success, "本人作品在 Ready 阶段读包是被允许的");

    CreatorToolRequest stale = good;
    stale.context.epoch = good.context.epoch - 1;
    const auto staleResult = workflow.AuthorizeToolCall(stale);
    Check(!staleResult.success, "旧 epoch 的工具调用被拒绝");
    CheckEq(staleResult.error.code, "epoch_stale", "错误码是 epoch_stale");

    CreatorToolRequest noWorkspace = good;
    noWorkspace.context.workspaceRoot.clear();
    CheckEq(workflow.AuthorizeToolCall(noWorkspace).error.code, "workspace_missing",
            "没有工作区的调用也被拒绝");
}

void TestWritingToolsNeedAnAuthoringStage() {
    const auto ready = MakeReady();
    // Ready 之后还能读、还能再采一份证据,但不能改内容 —— 改内容要开新一轮。
    CheckEq(ready.AuthorizeToolCall(ToolCall(ready, CreatorTool::PackageUpdate)).error.code,
            "stage_not_allowed", "Ready 阶段不接受会改候选的工具");
    Check(ready.AuthorizeToolCall(ToolCall(ready, CreatorTool::PackageRead)).success,
          "Ready 阶段可以读");
    Check(ready.AuthorizeToolCall(ToolCall(ready, CreatorTool::PreviewEvidence)).success,
          "Ready 阶段可以再采证据");

    // Draft 阶段:能查能力和读规则,不能碰候选。
    CreationWorkflow drafting(MakeSession("S-draft"), CreationBudget{});
    Check(drafting.AuthorizeToolCall(ToolCall(drafting, CreatorTool::SkillGet)).success,
          "Draft 阶段可以读规则");
    CheckEq(drafting.AuthorizeToolCall(ToolCall(drafting, CreatorTool::PackageUpdate)).error.code,
            "stage_not_allowed", "Draft 阶段不接受包写入");
    Check(drafting.AuthorizeToolCall(ToolCall(drafting, CreatorTool::CapabilitiesGet)).success,
          "Draft 阶段可以查能力");

    // 生成中:写工具放行。
    CreationWorkflow generating(MakeSession("S-gen"), CreationBudget{});
    generating.Apply({.type = CreationEventType::BriefSubmitted, .message = Msg(generating, 0)});
    generating.Apply({.type = CreationEventType::PreparationSucceeded,
                      .message = Msg(generating, 1)});
    Check(generating.AuthorizeToolCall(ToolCall(generating, CreatorTool::PackageUpdate)).success,
          "Generating 阶段接受包写入");

    // 取消之后一律不再动候选。
    generating.RequestCancel();
    CheckEq(generating.AuthorizeToolCall(ToolCall(generating, CreatorTool::PackageUpdate)).error.code,
            "cancelled", "取消后不接受包写入");
    CheckEq(generating.AuthorizeToolCall(ToolCall(generating, CreatorTool::PackageRead)).error.code,
            "cancelled", "取消后连读也拒绝(这一轮已经结束)");
}

void TestWallClockBudgetStopsSilentContinuation() {
    CreationWorkflow workflow(MakeSession("S-7"), CreationBudget{});
    workflow.Apply({.type = CreationEventType::BriefSubmitted, .message = Msg(workflow, 0)});
    workflow.Apply({.type = CreationEventType::PreparationSucceeded, .message = Msg(workflow, 1)});
    workflow.Apply({.type = CreationEventType::CandidateSubmitted, .message = Msg(workflow, 1),
                    .candidateId = "cand-1", .digest = "d", .summary = "s"});
    Check(!workflow.Budget().WallClockExhausted(), "刚开始没超时");

    workflow.AdvanceClock(workflow.Budget().wallClockLimitMs + 1);
    Check(workflow.Budget().WallClockExhausted(), "超过 10 分钟墙钟上限后判超时");
    workflow.Apply({.type = CreationEventType::ValidationFailed, .message = Msg(workflow, 1),
                    .candidateId = "cand-1", .digest = "d", .errorCode = "param_below_min",
                    .errorLocation = "loc", .detail = "坏参数"});
    Check(workflow.Stage() == CreationStage::Failed, "超时后不再静默续跑,直接停下并说明");
    Check(workflow.Budget().repairRoundsUsed == 0, "且没有偷偷用掉一轮修复");
}

void TestPreparationFailureReportsAndDoesNotPretend() {
    CreationWorkflow workflow(MakeSession("S-8"), CreationBudget{});
    const auto effects = workflow.Apply({.type = CreationEventType::PreparationFailed,
                                         .message = Msg(workflow, 0),
                                         .errorCode = "skill_unavailable",
                                         .detail = "Skills 未就绪"});
    Check(workflow.Stage() == CreationStage::Failed, "准备失败停在 Failed");
    Check(HasEffect(effects, CreationEffectType::RecordFailure), "并说明原因");
    Check(workflow.Session().turnId == 0, "没有假装进入过生成轮次");
}

void TestBriefInsufficientAsksUser() {
    CreationWorkflow workflow(MakeSession("S-9"), CreationBudget{});
    const auto effects = workflow.Apply({.type = CreationEventType::BriefInsufficient,
                                         .message = Msg(workflow, 0),
                                         .detail = "还需要目标显示器比例"});
    Check(workflow.Stage() == CreationStage::NeedsInput, "信息不足时停在 NeedsInput");
    Check(HasEffect(effects, CreationEffectType::Notify), "并说要补什么");
}

void TestDraftPersistHookSeesLastValidCandidate() {
    auto workflow = MakeReady();
    int calls = 0;
    std::string seenCandidate;
    workflow.SetDraftPersistHook([&](const CreationSession&, const CandidateRevision& candidate) {
        ++calls;
        seenCandidate = candidate.candidateId;
    });
    workflow.Apply({.type = CreationEventType::CancelRequested, .message = Msg(workflow, 1)});
    Check(calls >= 1, "阶段切换时宿主拿到持久化钩子");
    CheckEq(seenCandidate, "cand-1", "钩子带的是最后有效候选,用于关闭窗口后恢复");
}

} // namespace
} // namespace miaodesk::creator

int wmain() {
    using namespace miaodesk::creator;
    TestHappyPathReachesReady();
    TestModelTextCannotDriveApplied();
    TestOnlyAUserClickAtReadyStartsAnApply();
    TestValidCandidateCannotBeAppliedOffReady();
    TestCancelLeavesAReasonInsteadOfSilentlyDropping();
    TestOldTurnCallbackCannotOverwriteNewTurn();
    TestDuplicateSettleSaysSo();
    TestValidationFailureRetriesThenStops();
    TestPreviousCandidateSurvivesFailure();
    TestReusedCandidateIdDoesNotTakeAwayTheLastGoodOne();
    TestUnrepairableFailureDoesNotConsumeRepairBudget();
    TestCancelInvalidatesLateCallbacks();
    TestApplyStillSettlesAfterCancel();
    TestDoubleClickProducesOneApply();
    TestIdempotentApply();
    TestFailedApplyKeepsOriginalState();
    TestBriefRevisionStartsOver();
    TestVisualReviewSkippedIsRecorded();
    TestEvidenceFailureIsNotCoveredByStaticImage();
    TestWrongSessionRejected();
    TestToolCallCannotForgeSession();
    TestWritingToolsNeedAnAuthoringStage();
    TestWallClockBudgetStopsSilentContinuation();
    TestPreparationFailureReportsAndDoesNotPretend();
    TestBriefInsufficientAsksUser();
    TestDraftPersistHookSeesLastValidCandidate();

    std::printf("\nCCA-02 creation workflow: %d checks, %d failures\n", g_checks, g_failures);
    if (g_failures) {
        std::printf("ALL CHECKS FAILED\n");
        return 1;
    }
    std::printf("ALL CHECKS PASSED\n");
    return 0;
}
