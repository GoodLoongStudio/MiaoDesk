#pragma once

// 创作工作流协议与状态机(CCA-02)。
//
// 这个文件是 docs/CONTENT_CREATOR_AGENT_PLAN.md 第 4～5 节的最小实现,刻意做成
// **不依赖 Windows、不依赖 HWND、不自己调服务** 的纯逻辑:
//   * 宿主把事件喂进来,拿回一串"宿主该做什么"的 Effects,自己去执行;
//   * 执行结果再作为事件喂回来。
// 这样状态转换、预算、取消 epoch、迟到消息、版本和幂等全都能用可执行测试覆盖,
// 而不需要一台 Windows 机器或一个真的 Pi 进程。
//
// 为什么选"reducer + effects"而不是让控制器直接调 DesktopControlService:
// 计划 4.2 的状态表里几乎每一步都要"再校验一次""再采集一次",那是宿主的真实副作用。
// 让控制器直接持有服务句柄,测试就必须替身整个服务层;而副作用一旦被绕过,
// "模型文本不能驱动已应用"这类约束就没了编译期可见的执行点。

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace miaodesk::creator {

enum class ContentCreatorKind { None, Wallpaper, Widget };

enum class CreationStage {
    Draft,       // 需求已知但未提交
    NeedsInput,  // 缺必要信息,等用户回答
    Preparing,   // 冻结 brief 与运行能力
    Generating,  // 在专属工作区制作
    Validating,  // 宿主校验并封存候选
    Rendering,   // 采集候选实际效果
    Reviewing,   // 视觉/需求评审(无视觉能力时标记未审)
    Repairing,   // 携带定位信息局部修改
    Ready,       // 展示候选及未覆盖项
    Applying,    // 用户明确点击应用,已再校验,正在调用正式 API
    Applied,     // 正式 API 确认成功
    Cancelling,
    Cancelled,
    Failed,
    Queued,      // 排队等另一个创作任务结束;不算正在生成
};

// 定义在 .cpp 里,不要在头上写 constexpr:constexpr 隐含 inline,而定义不在
// 本 TU 内时,链接到一个不存在的符号上,表现是段错误而不是编译错误。
const char* ToString(CreationStage stage) noexcept;

// ---------------------------------------------------------------------------
// 4.1 四类宿主记录
// ---------------------------------------------------------------------------

// ID 一律由宿主生成。模型提交的路径、ID、成功声明都按输入处理,不能成为事实来源。
struct CreationBrief {
    std::string briefId;                 // 宿主生成
    std::uint64_t revision{1};           // 用户补充需求时 +1,冻结的那一份才有效
    ContentCreatorKind kind{ContentCreatorKind::None};
    std::string goal;                    // 用户目标
    std::string visualDirection;         // 视觉方向
    std::string aspectOrSize;            // 尺寸/比例
    std::string dataAndInteraction;      // 数据与交互需求
    std::string materialSource;          // 素材来源
    std::vector<std::string> allowedCapabilities;
    std::vector<std::string> confirmedLimits;   // 已向用户确认的限制
    std::vector<std::pair<std::string, std::string>> userParameters;

    bool Sufficient() const noexcept {
        return KindValid() && !goal.empty();
    }
    bool KindValid() const noexcept {
        return kind == ContentCreatorKind::Wallpaper || kind == ContentCreatorKind::Widget;
    }
};

// Provider 配置版本只记"哪一份、什么时候生效",不带凭据。
struct ProviderConfigStamp {
    std::string providerId;
    std::string model;
    std::string baseUrlHost;             // 主机名即可,写完整 URL 会把路径里的密钥带进状态
    std::string apiType;
    bool visionVerified{false};          // 是否已确证该 Provider 支持视觉输入
    std::uint64_t stamp{};
};

struct SkillStamp {
    std::string name;
    std::string version;
    std::string digest;
};

struct CreationSession {
    std::string sessionId;               // 宿主生成
    std::uint64_t briefRevision{};       // 冻结时的 brief.revision
    CreationStage stage{CreationStage::Draft};
    ProviderConfigStamp provider;
    std::vector<SkillStamp> skills;
    std::uint64_t turnId{};              // 当前轮次
    std::uint64_t epoch{};               // 停止请求后 +1,旧回调不得覆盖
    bool cancelRequested{false};

    bool Active() const noexcept {
        return stage != CreationStage::Applied && stage != CreationStage::Cancelled &&
               stage != CreationStage::Failed;
    }
};

// 校验、截图、评审、应用都绑定同一个摘要。候选修改即使路径相同,也是新 revision。
struct CandidateRevision {
    std::string candidateId;
    std::uint32_t revision{1};
    std::string packageId;               // 托管后的包 ID
    std::string digest;                  // 覆盖 manifest+scene+参数+引用素材
    std::string summary;
    std::string snapshotPath;            // 只读快照位置
    bool validated{false};
    std::string validationError;         // 结构化定位信息,不是给人看的散文
    std::string backend;
    bool evidenceCollected{false};
    bool visualReviewed{false};
    bool visualReviewSkipped{false};     // Provider 不支持视觉时为 true,不得谎称已审
    std::string visualReviewNote;
    std::uint64_t sourceTurn{};
};

// 可恢复前态必须精确,不能取"最近使用的第一项"。
struct OperationRecord {
    std::string operationId;             // 宿主生成,幂等键的一部分
    std::string sessionId;
    std::uint64_t turnId{};
    std::string candidateDigest;
    std::string target;                  // 显示器 ID 或组件放置目标
    bool started{false};
    // 正式 API 给过结论(成功或失败)。与 committed 分开:失败同样要收尾。
    bool resolved{false};
    bool committed{false};
    bool rolledBack{false};
    std::string beforeState;             // 精确前态
    std::string result;
    std::uint64_t completedAtMs{};
};

// ---------------------------------------------------------------------------
// 4.3 取消、版本和幂等
// ---------------------------------------------------------------------------

// 所有异步消息必须携带。接收端先验证归属,再考虑更新 UI、候选或记录。
struct CreationMessage {
    std::string sessionId;
    std::uint64_t epoch{};
    std::uint64_t turnId{};
    std::string operationId;             // 仅操作类消息需要

    bool Owns(const CreationSession& session) const noexcept {
        return sessionId == session.sessionId && epoch == session.epoch;
    }
    // 迟到消息:同一个 epoch/turn 的旧内容不该覆盖新结果。turnId 小于当前轮次即旧消息。
    bool Stale(const CreationSession& session) const noexcept {
        return turnId < session.turnId;
    }
};

// 应用幂等键 = 候选摘要 + 目标 + 用户操作 ID。
// 重复点击/回调返回同一结果;用户再次明确添加第二个组件是新 operationId,允许执行。
struct ApplyIdempotencyKey {
    std::string candidateDigest;
    std::string target;
    std::string operationId;

    bool operator==(const ApplyIdempotencyKey&) const noexcept = default;
    std::string Encode() const;
};

struct ApplyIdempotencyEntry {
    ApplyIdempotencyKey key;
    bool committed{false};
    // "已经拿到正式 API 的结论"。失败也是结论 —— 少了这个标志,一次 ApplyFailed
    // 会被当成还没结算,状态一直卡在 Applying,而用户看到的是应用没成功。
    bool resolved{false};
    std::string result;
    // 恢复要用的是精确前态,不是"最近使用的第一项"。所以账本记的就是它。
    std::string beforeState;
    OperationRecord record;
};

// ---------------------------------------------------------------------------
// 5. 创作工具契约(拟新增)
// ---------------------------------------------------------------------------

enum class CreatorTool {
    CapabilitiesGet,
    SkillGet,
    PackageRead,
    PackageUpdate,
    AssetImport,
    ImageGenerate,
    CandidateSubmit,
    PreviewEvidence,
};

// 每个工具的调用都必须带会话绑定的可信上下文。模型不能伪造 sessionId 访问另一个作品。
struct CreatorToolContext {
    std::string sessionId;
    std::uint64_t epoch{};
    std::string workspaceRoot;           // 该作品专属工作区
};

struct CreatorToolRequest {
    CreatorTool tool{};
    CreatorToolContext context;
    std::string argumentsJson;
};

// 返回结构化错误码,而不是一段散文。主界面保持可理解,技术原文进诊断详情。
struct CreatorToolError {
    std::string code;
    bool retryable{false};
    std::string location;                // 字段/节点/文件定位
    std::string detail;
};

struct CreatorToolOutcome {
    bool success{false};
    std::string payloadJson;
    CreatorToolError error;
};

// ---------------------------------------------------------------------------
// 6.3 初始预算
// ---------------------------------------------------------------------------

struct CreationBudget {
    std::uint32_t repairRoundsUsed{0};
    std::uint32_t repairRoundsMax{2};        // 结构与视觉修复共用计数
    std::uint32_t visualReviewsUsed{0};
    std::uint32_t visualReviewsMax{3};
    std::uint32_t evidenceFramesUsed{0};
    std::uint32_t evidenceFramesMax{6};
    std::uint32_t evidenceMaxEdgePx{1280};
    std::uint64_t evidenceMaxBytesPerRun{8ull * 1024 * 1024};
    std::uint64_t wallClockLimitMs{10ull * 60 * 1000};
    std::uint64_t startedAtMs{0};
    std::uint64_t nowMs{0};

    bool RepairExhausted() const noexcept { return repairRoundsUsed >= repairRoundsMax; }
    bool VisualReviewExhausted() const noexcept { return visualReviewsUsed >= visualReviewsMax; }
    bool EvidenceExhausted() const noexcept { return evidenceFramesUsed >= evidenceFramesMax; }
    std::uint64_t ElapsedMs() const noexcept {
        return nowMs >= startedAtMs ? nowMs - startedAtMs : 0;
    }
    bool WallClockExhausted() const noexcept { return ElapsedMs() >= wallClockLimitMs; }
};

// ---------------------------------------------------------------------------
// 事件与 Effects
// ---------------------------------------------------------------------------

enum class CreationEventType {
    BriefSubmitted,        // 用户提交了足够的需求
    BriefInsufficient,     // 用户提交了但缺必要信息
    BriefRevised,          // 用户补充需求
    PreparationSucceeded,
    PreparationFailed,
    CandidateSubmitted,    // 模型交出结构化候选(宿主不因文本就相信)
    ValidationSucceeded,
    ValidationFailed,
    EvidenceCollected,
    EvidenceFailed,
    ReviewCompleted,       // 有具体可修复问题 / 无阻塞问题 / 视觉未审
    RepairAttempted,
    RepairExhausted,
    ApplyRequested,        // 用户明确点击应用;唯一能进入 Applying 的事件
    ApplyCommitted,        // 正式 API 返回成功
    ApplyFailed,
    CancelRequested,
    CancelSettled,
    ServiceError,
    QueueDrained,
};

enum class CreationReviewVerdict {
    Blocking,             // 有具体可修复问题
    Clear,                // 无阻塞问题
    VisualNotReviewed,    // 无视觉能力;必须如实标记
};

// 字段顺序经过安排,让 designated initializer(.type = ..., .target = ...)在测试里
// 既按声明顺序又能读。曾经用位置初始化,结果把 operationId 写到了 target 上 ——
// 一个"缺 operationId"的假失败,查起来像状态机坏了。
struct CreationEvent {
    CreationEventType type{};
    CreationMessage message;
    std::string candidateId;
    std::string digest;
    std::string summary;
    CreationReviewVerdict reviewVerdict{CreationReviewVerdict::Clear};
    std::string reviewNote;
    std::string errorCode;
    std::string errorLocation;
    std::string detail;
    std::string target;                  // 应用目标:显示器 ID 或组件放置目标
    std::string operationId;             // ApplyRequested 的用户操作 ID
    std::string beforeState;             // 精确前态,恢复用
};

// 控制器只表达意图,具体动作由宿主执行。
enum class CreationEffectType {
    FreezeBriefAndCapabilities,
    OpenWorkspace,
    StartGenerationTurn,
    ValidateCandidate,
    SnapshotCandidate,
    CollectEvidence,
    RunVisualReview,
    RequestRepair,
    PresentReady,
    BeginApply,
    ConfirmApply,
    RecordFailure,
    ReleaseResources,
    Notify,
};

struct CreationEffect {
    CreationEffectType type{};
    std::string sessionId;
    std::uint64_t epoch{};
    std::uint64_t turnId{};
    std::string candidateId;
    std::string digest;
    std::string target;
    std::string operationId;
    std::string detail;
    bool budgetExhausted{false};
};

// 状态机。一个实例对应一个作品会话。
class CreationWorkflow {
public:
    explicit CreationWorkflow(CreationSession session, CreationBudget budget = {});

    const CreationSession& Session() const noexcept { return session_; }
    const CreationBudget& Budget() const noexcept { return budget_; }
    const std::vector<CandidateRevision>& Candidates() const noexcept { return candidates_; }
    CreationStage Stage() const noexcept { return session_.stage; }

    // 喂入一个事件,返回宿主要执行的动作。
    // 归属不对、已取消、已终态的事件一律被拒绝,并留下拒绝原因而不是静默丢弃。
    std::vector<CreationEffect> Apply(const CreationEvent& event);

    // 用户显式请求停止。epoch 递增,之后旧回调全部失效;
    // 但已经开始的应用事务仍要凭正式 API 的真实结果结算(见 ApplyCommitted/ApplyFailed)。
    void RequestCancel();

    // 宿主推进自己的时钟。墙钟上限由控制器判,但时间必须由宿主给 ——
    // 否则测试要么 sleep,要么永远跑不到超时那一支。
    void AdvanceClock(std::uint64_t nowMs) noexcept { budget_.nowMs = nowMs; }

    // 记一笔应用操作。resolved=false 是 BeginApply("已发出、还没有结论");
    // resolved=true 是正式 API 给过结论 —— 成功和失败都算结论。
    // 幂等:同一候选摘要+同一目标+同一用户操作 ID 只结算一次。
    // 返回值表示"这次调用是不是第一次给出结论",宿主据此决定要不要改阶段。
    bool RecordApplyOutcome(const ApplyIdempotencyKey& key, bool committed, bool resolved,
                            std::string result, std::string beforeState,
                            std::uint64_t completedAtMs);

    // 应用前重验:候选摘要与目标仍有效才允许进入正式 API。
    // 已成功过的同一操作返回 false;正在进行的同一操作返回 true。
    bool ApplyPreconditionHolds(const ApplyIdempotencyKey& key) const;

    // 这一笔是否已经发出去、还没拿到正式 API 的结论。
    bool IsApplyInFlight(const ApplyIdempotencyKey& key) const;

    const std::vector<ApplyIdempotencyEntry>& ApplyLedger() const noexcept { return applyLedger_; }

    // 新作品进来时调用:保存/恢复草稿与最后有效候选用的钩子,由宿主实现持久化。
    using DraftPersist = std::function<void(const CreationSession&, const CandidateRevision&)>;
    void SetDraftPersistHook(DraftPersist hook) { draftPersist_ = std::move(hook); }

    CandidateRevision* FindCandidate(const std::string& candidateId);
    const CandidateRevision* FindCandidate(const std::string& candidateId) const;

    // 最近一个通过校验的候选。取消/失败后仍然可查,用于"上一有效候选"。
    const CandidateRevision* LastValidCandidate() const noexcept;

    // 当前这一轮正在处理的那一个候选。按 (candidateId, digest) 一起认 ——
    // 见 pendingCandidateId_ 上面那段。ID 单独一个不够。
    CandidateRevision* FindPendingCandidate();
    const CandidateRevision* FindPendingCandidate() const;

    // 工具调用是否被允许。模型伪造/复用别的 sessionId 一律拒绝。
    CreatorToolOutcome AuthorizeToolCall(const CreatorToolRequest& request) const;

    std::string LastRejectionReason() const noexcept { return lastRejection_; }

private:
    bool Terminal() const noexcept;
    // 非 const:拒绝要把原因留下来。调用方需要能说清"为什么这条消息没有改变状态",
    // 而不是让一条迟到消息静默消失。
    bool Accepts(const CreationMessage& message, const CreationEvent& event);
    void Enter(CreationStage stage);
    std::vector<CreationEffect> Notify(std::string detail) const;

    CreationSession session_;
    CreationBudget budget_;
    std::vector<CandidateRevision> candidates_;
    std::vector<ApplyIdempotencyEntry> applyLedger_;
    // 正在校验的那一个候选。**ID 和摘要必须一起记。**
    //
    // 只记 ID 出过真事:`ValidationFailed`(以及 Evidence/Review 几条)按
    // `FindCandidate(pendingCandidateId_)` 找到谁就改谁。模型复用一个 candidateId 时,
    // 找到的是**上一轮那个已经成功的**候选,于是 `validated = false` 落在那条记录上 ——
    // 新一轮的失败把用户上一版可预览的结果带走了。那正是 CREATE-04 的验收:
    // "新一轮生成/修复失败不覆盖上一份可预览结果"。
    // 而现有测试之所以是绿的,只因为它给第二轮用了不同的 ID。
    std::string pendingCandidateId_;
    std::string pendingDigest_;
    std::string lastRejection_;
    DraftPersist draftPersist_;
};

} // namespace miaodesk::creator
