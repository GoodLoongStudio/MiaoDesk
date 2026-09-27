#include "miaodesk/CreatorRuntimeScheduler.h"

#include <algorithm>

namespace miaodesk::creator {
namespace {

CreatorRuntimeEffect MakeEffect(CreatorRuntimeEffectType type, const CreatorRuntimeRequest& request) {
    CreatorRuntimeEffect effect;
    effect.type = type;
    effect.kind = request.kind;
    effect.sessionId = request.sessionId;
    effect.workspaceRoot = request.workspaceRoot;
    return effect;
}

CreatorRuntimeEffect MakeEffect(CreatorRuntimeEffectType type, const std::string& sessionId) {
    CreatorRuntimeEffect effect;
    effect.type = type;
    effect.sessionId = sessionId;
    return effect;
}

} // namespace

const char* ToString(CreatorRuntimeState state) noexcept {
    switch (state) {
    case CreatorRuntimeState::Idle: return "Idle";
    case CreatorRuntimeState::Starting: return "Starting";
    case CreatorRuntimeState::Active: return "Active";
    case CreatorRuntimeState::Releasing: return "Releasing";
    }
    return "Unknown";
}

const char* ToString(CreatorRuntimeEventType type) noexcept {
    switch (type) {
    case CreatorRuntimeEventType::OpenRequested: return "OpenRequested";
    case CreatorRuntimeEventType::DraftReopenRequested: return "DraftReopenRequested";
    case CreatorRuntimeEventType::ProcessStarted: return "ProcessStarted";
    case CreatorRuntimeEventType::ProcessStartFailed: return "ProcessStartFailed";
    case CreatorRuntimeEventType::TurnStarted: return "TurnStarted";
    case CreatorRuntimeEventType::TurnSettled: return "TurnSettled";
    case CreatorRuntimeEventType::CloseRequested: return "CloseRequested";
    case CreatorRuntimeEventType::CancelRequested: return "CancelRequested";
    case CreatorRuntimeEventType::IdleTick: return "IdleTick";
    case CreatorRuntimeEventType::ReleaseConfirmed: return "ReleaseConfirmed";
    case CreatorRuntimeEventType::ReleaseFailed: return "ReleaseFailed";
    }
    return "Unknown";
}

const char* ToString(CreatorRuntimeEffectType type) noexcept {
    switch (type) {
    case CreatorRuntimeEffectType::StartProcess: return "StartProcess";
    case CreatorRuntimeEffectType::StopProcess: return "StopProcess";
    case CreatorRuntimeEffectType::ReportQueued: return "ReportQueued";
    case CreatorRuntimeEffectType::ReportReleased: return "ReportReleased";
    case CreatorRuntimeEffectType::ReportRejected: return "ReportRejected";
    }
    return "Unknown";
}

bool CreatorRuntimeScheduler::IdleDueAt(std::uint64_t nowMs) const noexcept {
    if (state_ != CreatorRuntimeState::Active) return false;
    if (nowMs < lastActivityMs_) return false;
    return nowMs - lastActivityMs_ >= kCreatorIdleReleaseMs;
}

bool CreatorRuntimeScheduler::SessionIsActive(const std::string& sessionId) const noexcept {
    return !activeSessionId_.empty() && activeSessionId_ == sessionId;
}

std::vector<CreatorRuntimeEffect> CreatorRuntimeScheduler::StartNext(std::uint64_t nowMs) {
    std::vector<CreatorRuntimeEffect> effects;
    if (state_ != CreatorRuntimeState::Idle) return effects;   // 只有一个名额
    if (queue_.empty()) return effects;

    active_ = queue_.front();
    queue_.erase(queue_.begin());
    activeSessionId_ = active_.sessionId;
    activeSinceMs_ = nowMs;
    lastActivityMs_ = nowMs;
    state_ = CreatorRuntimeState::Starting;
    effects.push_back(MakeEffect(CreatorRuntimeEffectType::StartProcess, active_));
    return effects;
}

void CreatorRuntimeScheduler::Enqueue(const CreatorRuntimeRequest& request,
                                     std::vector<CreatorRuntimeEffect>* effects) {
    // 同一个会话已在队列里就不重复排:用户连点两下"打开创作"应该只排一次。
    for (const auto& pending : queue_) {
        if (pending.sessionId == request.sessionId) {
            CreatorRuntimeEffect effect;
            effect.type = CreatorRuntimeEffectType::ReportQueued;
            effect.kind = request.kind;
            effect.sessionId = request.sessionId;
            std::uint32_t ahead = 0;
            for (const auto& other : queue_) {
                if (other.sessionId == request.sessionId) break;
                ++ahead;
            }
            effect.queueAhead = ahead;
            effects->push_back(effect);
            return;
        }
    }

    CreatorRuntimeRequest pending = request;
    pending.enqueuedAtMs = lastActivityMs_ ? lastActivityMs_ : 0;
    queue_.push_back(pending);

    CreatorRuntimeEffect effect;
    effect.type = CreatorRuntimeEffectType::ReportQueued;
    effect.kind = request.kind;
    effect.sessionId = request.sessionId;
    effect.queueAhead = static_cast<std::uint32_t>(queue_.size() - 1);
    effects->push_back(effect);
}

std::vector<CreatorRuntimeEffect> CreatorRuntimeScheduler::Apply(const CreatorRuntimeEvent& event) {
    std::vector<CreatorRuntimeEffect> effects;

    switch (event.type) {
    case CreatorRuntimeEventType::OpenRequested:
    case CreatorRuntimeEventType::DraftReopenRequested: {
        const CreatorRuntimeRequest& request = event.request;
        if (request.sessionId.empty() || request.workspaceRoot.empty()) {
            // 没有绑定就不该占名额。一个说不清属于哪个作品的会话,
            // 起起来也说不清该往哪写。
            CreatorRuntimeEffect effect;
            effect.type = CreatorRuntimeEffectType::ReportRejected;
            effect.kind = request.kind;
            effect.detail = "这次创作请求没有会话或工作区绑定,已拒绝。";
            effects.push_back(effect);
            return effects;
        }
        // 同一个会话已经在跑:不重复起,也不排队 —— 用户只是又点了一下。
        if (SessionIsActive(request.sessionId)) {
            if (event.type == CreatorRuntimeEventType::DraftReopenRequested) {
                lastActivityMs_ = event.nowMs ? event.nowMs : lastActivityMs_;
            }
            return effects;
        }
        // 已排队:见 Enqueue 的去重。
        bool queued = false;
        for (const auto& pending : queue_) {
            if (pending.sessionId == request.sessionId) queued = true;
        }
        if (queued || state_ != CreatorRuntimeState::Idle) {
            Enqueue(request, &effects);
            return effects;
        }
        queue_.push_back(request);
        auto started = StartNext(event.nowMs);
        effects.insert(effects.end(), started.begin(), started.end());
        return effects;
    }

    case CreatorRuntimeEventType::ProcessStarted: {
        if (state_ != CreatorRuntimeState::Starting) {
            // 迟到回报:这个进程可能已经因为我们发了 StopProcess 而没了。
            // 不据它改状态,但要让宿主知道有一条对不上的消息。
            CreatorRuntimeEffect effect;
            effect.type = CreatorRuntimeEffectType::ReportRejected;
            effect.sessionId = event.sessionId;
            effect.detail = "进程启动回报来迟了,当前并不在等待启动,已忽略。";
            effects.push_back(effect);
            return effects;
        }
        if (!event.sessionId.empty() && !SessionIsActive(event.sessionId)) {
            CreatorRuntimeEffect effect;
            effect.type = CreatorRuntimeEffectType::ReportRejected;
            effect.sessionId = event.sessionId;
            effect.detail = "启动回报的会话与当前活跃会话不符,已忽略。";
            effects.push_back(effect);
            return effects;
        }
        state_ = CreatorRuntimeState::Active;
        lastActivityMs_ = event.nowMs ? event.nowMs : lastActivityMs_;
        return effects;
    }

    case CreatorRuntimeEventType::ProcessStartFailed: {
        if (state_ != CreatorRuntimeState::Starting) return effects;
        // 起失败不能卡在 Starting:那既不给用户反馈,又占着唯一的名额。
        // 报告失败,然后立刻把名额让给队列里的下一个。
        {
            CreatorRuntimeEffect effect;
            effect.type = CreatorRuntimeEffectType::ReportRejected;
            effect.sessionId = activeSessionId_;
            effect.detail = event.reason.empty() ? std::string("创作进程启动失败。")
                                                : event.reason;
            effects.push_back(effect);
        }
        activeSessionId_.clear();
        active_ = CreatorRuntimeRequest{};
        state_ = CreatorRuntimeState::Idle;
        auto next = StartNext(event.nowMs);
        effects.insert(effects.end(), next.begin(), next.end());
        return effects;
    }

    case CreatorRuntimeEventType::TurnStarted:
    case CreatorRuntimeEventType::TurnSettled: {
        // 活跃信号只对当前会话算数。别的会话的活动不能替这个会话续命 ——
        // 否则两个作品会互相把对方的空闲计时器顶掉,而两者都不释放。
        if (!SessionIsActive(event.sessionId)) return effects;
        if (state_ != CreatorRuntimeState::Active) return effects;
        if (event.nowMs > lastActivityMs_) lastActivityMs_ = event.nowMs;
        return effects;
    }

    case CreatorRuntimeEventType::CloseRequested:
    case CreatorRuntimeEventType::CancelRequested: {
        // 排队里的请求**先**处理,而且不看当前状态。
        //
        // 第一版这里判了 `state_ == Idle` 才去撤队列,于是"另一个创作正在跑时,
        // 用户关掉自己那个排队的"会落到下面"停掉当前进程"那条路上 —— 他关的是
        // 自己那一轮,却被执行成取消别人的作品。这正是"取消只影响它自己那一轮"
        // 要防的事,而它只在 Active 状态同时有排队时才发生。
        {
            const auto before = queue_.size();
            queue_.erase(std::remove_if(queue_.begin(), queue_.end(),
                                        [&event](const CreatorRuntimeRequest& pending) {
                                            return !event.sessionId.empty() &&
                                                   pending.sessionId == event.sessionId;
                                        }),
                         queue_.end());
            if (queue_.size() != before) {
                CreatorRuntimeEffect effect;
                effect.type = CreatorRuntimeEffectType::ReportRejected;
                effect.sessionId = event.sessionId;
                effect.detail = "已从排队里撤掉。";
                effects.push_back(effect);
                return effects;
            }
        }
        if (state_ == CreatorRuntimeState::Idle || state_ == CreatorRuntimeState::Releasing) {
            return effects;
        }
        if (!event.sessionId.empty() && !SessionIsActive(event.sessionId)) {
            // 取消必须只影响它自己那一轮。会话对不上就什么都不做 ——
            // 一个说不清归属的取消,执行下去就是在替别人的作品做决定。
            CreatorRuntimeEffect effect;
            effect.type = CreatorRuntimeEffectType::ReportRejected;
            effect.sessionId = event.sessionId;
            effect.detail = "取消请求的会话不是当前活跃会话,已忽略。";
            effects.push_back(effect);
            return effects;
        }
        state_ = CreatorRuntimeState::Releasing;
        effects.push_back(MakeEffect(CreatorRuntimeEffectType::StopProcess, activeSessionId_));
        return effects;
    }

    case CreatorRuntimeEventType::ReleaseConfirmed: {
        if (state_ != CreatorRuntimeState::Releasing) {
            CreatorRuntimeEffect effect;
            effect.type = CreatorRuntimeEffectType::ReportRejected;
            effect.sessionId = event.sessionId;
            effect.detail = "释放回报来迟了,当前并不在等待释放,已忽略。";
            effects.push_back(effect);
            return effects;
        }
        // 释放成功:名额交出去,队列里的下一个顶上。
        effects.push_back(MakeEffect(CreatorRuntimeEffectType::ReportReleased, activeSessionId_));
        activeSessionId_.clear();
        active_ = CreatorRuntimeRequest{};
        state_ = CreatorRuntimeState::Idle;
        auto next = StartNext(event.nowMs);
        effects.insert(effects.end(), next.begin(), next.end());
        return effects;
    }

    case CreatorRuntimeEventType::ReleaseFailed: {
        if (state_ != CreatorRuntimeState::Releasing) return effects;
        // 释放失败时**不能**当它已经释放了。假装成功的后果是下一个请求起来时
        // 上一个进程还活着 —— 那正是"多次开关后进程持续增长"的来路。
        // 停在 Releasing,让宿主重试;队列里的请求继续等。
        CreatorRuntimeEffect effect;
        effect.type = CreatorRuntimeEffectType::ReportRejected;
        effect.sessionId = activeSessionId_;
        effect.detail = "创作进程没有停下来,已要求宿主重试;排队中的请求继续等待。";
        effects.push_back(effect);
        return effects;
    }

    case CreatorRuntimeEventType::IdleTick:
        return Tick(event.nowMs);
    }
    return effects;
}

std::vector<CreatorRuntimeEffect> CreatorRuntimeScheduler::Tick(std::uint64_t nowMs) {
    std::vector<CreatorRuntimeEffect> effects;
    // 注意这里不能加 "activeSinceMs_ != 0" 之类的守卫:t=0 是一个合法时间戳,
    // 而加上它之后,"在 0 时刻打开的创作"永远不可能启动超时 —— 它卡在 Starting,
    // 占着全产品唯一的名额,而用户看不到任何反馈。state_ == Starting 已经保证了
    // activeSinceMs_ 被设过,再要一个哨兵值只是多余且有害。
    if (state_ == CreatorRuntimeState::Starting && nowMs > activeSinceMs_ &&
        nowMs - activeSinceMs_ >= kCreatorStartTimeoutMs) {
        // 起太久:收回名额。卡在 Starting 比没有更糟 —— 它占着唯一的名额,
        // 而用户看不到任何反馈。
        CreatorRuntimeEffect effect;
        effect.type = CreatorRuntimeEffectType::ReportRejected;
        effect.sessionId = activeSessionId_;
        effect.detail = "创作进程启动超时,已收回名额。";
        effects.push_back(effect);
        activeSessionId_.clear();
        active_ = CreatorRuntimeRequest{};
        state_ = CreatorRuntimeState::Idle;
        auto next = StartNext(nowMs);
        effects.insert(effects.end(), next.begin(), next.end());
        return effects;
    }
    if (!IdleDueAt(nowMs)) return effects;
    state_ = CreatorRuntimeState::Releasing;
    effects.push_back(MakeEffect(CreatorRuntimeEffectType::StopProcess, activeSessionId_));
    return effects;
}

} // namespace miaodesk::creator
