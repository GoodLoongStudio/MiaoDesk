#pragma once

// CCA-03:按需创作运行实例的调度(纯逻辑)。
//
// 计划对这一条的验收是「同时最多一个创作任务」「聊天生成中可独立取消/重置创作且不
// 影响聊天」「多次开关后无持续进程增长」「Provider 修改有明确生效时机」。前三条
// 都能在这里被断言,不需要一台 Windows 机器 —— 因为它们本质上是**谁在什么时候
// 拿到进程**的问题,而不是进程本身的问题。
//
// 为什么不在 PiRuntime 里直接写:PiRuntime 持有真实句柄、真实 CreateProcessW,
// 于是「第二个创作请求进来时会发生什么」只能在 Windows 上试。而这里刻意只做
// 决策:宿主拿着 StartProcess / StopProcess 这类 effect 自己去执行,执行结果再作为
// 事件喂回来。判错的代价因此在本机就看得见,而不是留到用户第二次打开创作时才炸。
//
// 它不 import 任何 Windows 头,也不认识 PiRuntime:调度与"怎么起一个进程"是两件事。
#include <cstdint>
#include <string>
#include <vector>

#include "miaodesk/CreationWorkflow.h"

namespace miaodesk::creator {

// 默认空闲释放时间。用户开着创作窗口但不说话,超过它就收回进程 ——
// 一个常驻的 Pi 进程是真实的内存与句柄,而"用户没在用"不是保留它的理由。
constexpr std::uint64_t kCreatorIdleReleaseMs = 5 * 60 * 1000;
// 起进程的超时。卡在 Starting 比没有更糟:它既不给用户反馈,又占着"活跃"这个名额。
constexpr std::uint64_t kCreatorStartTimeoutMs = 30 * 1000;

enum class CreatorRuntimeState {
    Idle,       // 没有创作进程
    Starting,   // 已要求启动,等宿主回报
    Active,     // 进程活着,且被本调度独占
    Releasing,  // 已要求停止,等宿主回报
};

const char* ToString(CreatorRuntimeState state) noexcept;

// 一次"打开创作"的请求。sessionId 与 workspaceRoot 由宿主分配。
struct CreatorRuntimeRequest {
    ContentCreatorKind kind{ContentCreatorKind::None};
    std::string sessionId;
    std::string workspaceRoot;
    bool reopen{false};        // 重开既有草稿,而不是新建
    std::uint64_t enqueuedAtMs{};
};

enum class CreatorRuntimeEventType {
    OpenRequested,        // 用户打开某个 kind 的创作
    DraftReopenRequested, // 用户重开一份既有草稿
    ProcessStarted,       // 宿主回报进程起来了
    ProcessStartFailed,
    TurnStarted,          // 一轮生成开始(活跃信号)
    TurnSettled,          // 一轮生成结束(活跃信号)
    CloseRequested,       // 用户主动关闭创作窗口
    CancelRequested,      // 用户取消当前这一轮
    IdleTick,             // 宿主的周期心跳,带当前时间
    ReleaseConfirmed,     // 宿主回报进程真的停了
    ReleaseFailed,
};

const char* ToString(CreatorRuntimeEventType type) noexcept;

struct CreatorRuntimeEvent {
    CreatorRuntimeEventType type{};
    CreatorRuntimeRequest request;
    std::string sessionId;
    std::string reason;
    std::uint64_t nowMs{};
};

enum class CreatorRuntimeEffectType {
    StartProcess,      // 宿主该起一个创作进程
    StopProcess,       // 宿主该停掉当前创作进程
    ReportQueued,      // 告诉用户"排队中,前面还有 N 个"
    ReportReleased,    // 告诉用户"创作进程已收回,草稿还在"
    ReportRejected,    // 这次请求被拒,原因见 detail
};

const char* ToString(CreatorRuntimeEffectType type) noexcept;

struct CreatorRuntimeEffect {
    CreatorRuntimeEffectType type{};
    ContentCreatorKind kind{ContentCreatorKind::None};
    std::string sessionId;
    std::string workspaceRoot;
    std::string detail;
    std::uint32_t queueAhead{0};     // ReportQueued:前面还有几个
};

class CreatorRuntimeScheduler {
public:
    CreatorRuntimeScheduler() = default;

    CreatorRuntimeState State() const noexcept { return state_; }
    // 当前被占用的会话。Idle 时为空。
    const std::string& ActiveSessionId() const noexcept { return activeSessionId_; }
    std::size_t QueueDepth() const noexcept { return queue_.size(); }
    bool IsIdle() const noexcept { return state_ == CreatorRuntimeState::Idle && queue_.empty(); }

    // 喂一个事件,拿到宿主该做的事。纯函数:同样的事件序列永远得到同样的结论。
    std::vector<CreatorRuntimeEffect> Apply(const CreatorRuntimeEvent& event);

    // 宿主的周期心跳。单独一个入口是因为它不必构造一整个事件。
    std::vector<CreatorRuntimeEffect> Tick(std::uint64_t nowMs);

    // 到 nowMs 为止,这个会话是否已经空闲到该被收回。公开是为了让宿主能自己
    // 判断"要不要现在 Tick",也让这个判据本身可被断言。
    bool IdleDueAt(std::uint64_t nowMs) const noexcept;

    // 供测试与诊断:当前活跃请求是什么(Releasing 期间仍可查)。
    const CreatorRuntimeRequest& ActiveRequest() const noexcept { return active_; }

private:
    std::vector<CreatorRuntimeEffect> StartNext(std::uint64_t nowMs);
    void Enqueue(const CreatorRuntimeRequest& request, std::vector<CreatorRuntimeEffect>* effects);
    bool SessionIsActive(const std::string& sessionId) const noexcept;

    CreatorRuntimeState state_{CreatorRuntimeState::Idle};
    CreatorRuntimeRequest active_;
    std::string activeSessionId_;
    std::vector<CreatorRuntimeRequest> queue_;
    std::uint64_t lastActivityMs_{};
    std::uint64_t activeSinceMs_{};
};

} // namespace miaodesk::creator
