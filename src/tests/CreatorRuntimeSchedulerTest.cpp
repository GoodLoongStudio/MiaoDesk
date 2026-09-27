// CCA-03:按需创作运行实例的调度。
//
// 这一条的验收是「同时最多一个创作任务」「聊天生成中可独立取消/重置创作且不影响
// 聊天」「多次开关后无持续进程增长」「Provider 修改有明确生效时机」。计划原文说
// 它的验收需要 Windows 真机 —— 那指的是"进程真的没有泄漏"。而**谁在什么时候
// 拿到名额**这件事是纯逻辑,可以在任何机器上被断言,而且它恰好是泄漏的成因:
// 真正让进程堆起来的是"发起了一次启动,而当时已经有一个活着"。所以这里把后者
// 钉死,前者才有一个不会发生的理由。
//
// 全部在本机实跑:调度不 import Windows 头,也不认识 PiRuntime。
#include "miaodesk/CreatorRuntimeScheduler.h"

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

CreatorRuntimeRequest Req(const char* session, ContentCreatorKind kind = ContentCreatorKind::Wallpaper,
                          const char* workspace = nullptr) {
    CreatorRuntimeRequest request;
    request.kind = kind;
    request.sessionId = session;
    request.workspaceRoot = workspace ? workspace : (std::string("C:\\ws\\") + session);
    return request;
}

CreatorRuntimeEvent Ev(CreatorRuntimeEventType type, std::uint64_t now = 1000) {
    CreatorRuntimeEvent event;
    event.type = type;
    event.nowMs = now;
    return event;
}

CreatorRuntimeEvent Open(const CreatorRuntimeRequest& request, std::uint64_t now = 1000,
                         bool reopen = false) {
    CreatorRuntimeEvent event;
    event.type = reopen ? CreatorRuntimeEventType::DraftReopenRequested
                        : CreatorRuntimeEventType::OpenRequested;
    event.request = request;
    event.nowMs = now;
    return event;
}

std::size_t CountEffects(const std::vector<CreatorRuntimeEffect>& effects,
                         CreatorRuntimeEffectType type) {
    std::size_t count = 0;
    for (const auto& effect : effects) {
        if (effect.type == type) ++count;
    }
    return count;
}

const CreatorRuntimeEffect* FindEffect(const std::vector<CreatorRuntimeEffect>& effects,
                                       CreatorRuntimeEffectType type) {
    for (const auto& effect : effects) {
        if (effect.type == type) return &effect;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// 1. 同时最多一个创作任务
// ---------------------------------------------------------------------------

void TestOnlyOneCreationAtATime() {
    CreatorRuntimeScheduler scheduler;
    Check(scheduler.IsIdle(), "起初是空闲的");

    const auto started = scheduler.Apply(Open(Req("S-1")));
    Check(CountEffects(started, CreatorRuntimeEffectType::StartProcess) == 1, "第一个请求起一个进程");
    Check(scheduler.State() == CreatorRuntimeState::Starting, "进入 Starting");
    Check(scheduler.ActiveSessionId() == "S-1", "且名额被 S-1 占着");

    // 第二个不同作品的请求:排队,不是再起一个。
    const auto queued = scheduler.Apply(Open(Req("S-2")));
    Check(CountEffects(queued, CreatorRuntimeEffectType::StartProcess) == 0,
          "第二个作品的请求不再起进程");
    Check(CountEffects(queued, CreatorRuntimeEffectType::ReportQueued) == 1, "而是告诉用户排队中");
    Check(scheduler.QueueDepth() == 1, "队列里有一个");
    CheckEq(ToString(scheduler.State()), "Starting", "状态没被第二个请求改变");

    // 起来之后第二个仍然不能插队。
    const auto live = scheduler.Apply(Ev(CreatorRuntimeEventType::ProcessStarted, 2000));
    Check(live.empty(), "启动回报不产生额外动作");
    CheckEq(ToString(scheduler.State()), "Active", "进入 Active");
    const auto stillQueued = scheduler.Apply(Open(Req("S-3")));
    Check(CountEffects(stillQueued, CreatorRuntimeEffectType::StartProcess) == 0,
          "已经有一个活着时,第三个请求仍然只排队");
    Check(scheduler.QueueDepth() == 2, "队列里有两个");
}

void TestARequestWithoutABindingIsRefused() {
    // 没有会话或工作区的请求不该占名额。起起来也说不清该往哪写 ——
    // 而这个名额是全产品唯一的一个。
    CreatorRuntimeScheduler scheduler;
    CreatorRuntimeRequest empty;
    empty.kind = ContentCreatorKind::Wallpaper;
    empty.sessionId = "S-1";
    empty.workspaceRoot.clear();
    const auto refused = scheduler.Apply(Open(empty));
    Check(CountEffects(refused, CreatorRuntimeEffectType::StartProcess) == 0, "没有工作区的请求不起进程");
    Check(CountEffects(refused, CreatorRuntimeEffectType::ReportRejected) == 1, "而是明确拒绝");
    Check(scheduler.IsIdle(), "且没有占名额");
}

// ---------------------------------------------------------------------------
// 2. 取消必须只影响它自己那一轮
// ---------------------------------------------------------------------------

void TestCancelOnlyAffectsItsOwnSession() {
    CreatorRuntimeScheduler scheduler;
    scheduler.Apply(Open(Req("S-1")));
    scheduler.Apply(Ev(CreatorRuntimeEventType::ProcessStarted, 2000));

    // 一个说不清归属的取消。执行下去就是在替别人的作品做决定。
    CreatorRuntimeEvent event = Ev(CreatorRuntimeEventType::CancelRequested, 3000);
    event.sessionId = "S-2";
    const auto wrong = scheduler.Apply(event);
    Check(CountEffects(wrong, CreatorRuntimeEffectType::StopProcess) == 0,
          "取消别的会话不会停掉当前这个");
    Check(CountEffects(wrong, CreatorRuntimeEffectType::ReportRejected) == 1, "且说明为什么没执行");
    CheckEq(ToString(scheduler.State()), "Active", "状态没变");

    // 取消自己的:停,然后名额交出去。
    CreatorRuntimeEvent mine = Ev(CreatorRuntimeEventType::CancelRequested, 4000);
    mine.sessionId = "S-1";
    const auto stopped = scheduler.Apply(mine);
    Check(CountEffects(stopped, CreatorRuntimeEffectType::StopProcess) == 1, "取消自己这一轮会停进程");
    CheckEq(ToString(scheduler.State()), "Releasing", "进入 Releasing");

    const auto done = scheduler.Apply(Ev(CreatorRuntimeEventType::ReleaseConfirmed, 5000));
    Check(CountEffects(done, CreatorRuntimeEffectType::ReportReleased) == 1, "释放完成有回报");
    Check(scheduler.IsIdle(), "回到空闲");
}

void TestClosingAQueuedRequestRemovesItFromTheQueue() {
    CreatorRuntimeScheduler scheduler;
    scheduler.Apply(Open(Req("S-1")));
    scheduler.Apply(Ev(CreatorRuntimeEventType::ProcessStarted, 2000));
    scheduler.Apply(Open(Req("S-2")));
    scheduler.Apply(Open(Req("S-3")));
    Check(scheduler.QueueDepth() == 2, "队列里有两个");

    CreatorRuntimeEvent cancel = Ev(CreatorRuntimeEventType::CloseRequested, 3000);
    cancel.sessionId = "S-2";
    const auto removed = scheduler.Apply(cancel);
    Check(CountEffects(removed, CreatorRuntimeEffectType::ReportRejected) == 1, "撤掉排队有回报");
    Check(scheduler.QueueDepth() == 1, "队列里剩一个");
    Check(CountEffects(removed, CreatorRuntimeEffectType::StopProcess) == 0,
          "且没有去停正在跑的那个 —— 撤排队不等于取消当前创作");
}

// ---------------------------------------------------------------------------
// 3. 多次开关后无持续进程增长
// ---------------------------------------------------------------------------

void TestRepeatedOpenCloseNeverStacksAProcess() {
    // 这一条把"进程增长"的成因钉住:每一次 StartProcess 都必须在没有活跃进程的
    // 前提下发出。真机上进程有没有泄漏要 Windows 才能看,但这里能让"发起了一次
    // 启动而当时已经有一个活着"这件事不可能发生。
    CreatorRuntimeScheduler scheduler;
    std::uint64_t now = 1000;
    for (int round = 0; round < 12; ++round) {
        const auto session = "S-" + std::to_string(round);
        CreatorRuntimeRequest request = Req(session.c_str());
        request.workspaceRoot = "C:\\ws\\" + session;
        const auto opened = scheduler.Apply(Open(request, now));
        Check(CountEffects(opened, CreatorRuntimeEffectType::StartProcess) == 1,
              "第 " + std::to_string(round) + " 轮:发出一次启动");
        Check(scheduler.State() == CreatorRuntimeState::Starting, "且进入 Starting");

        const auto live = scheduler.Apply(Ev(CreatorRuntimeEventType::ProcessStarted, now + 100));
        Check(live.empty(), "启动回报干净");

        CreatorRuntimeEvent close = Ev(CreatorRuntimeEventType::CloseRequested, now + 200);
        close.sessionId = session;
        const auto stopping = scheduler.Apply(close);
        Check(CountEffects(stopping, CreatorRuntimeEffectType::StopProcess) == 1, "要求停止");

        const auto released = scheduler.Apply(Ev(CreatorRuntimeEventType::ReleaseConfirmed, now + 300));
        Check(CountEffects(released, CreatorRuntimeEffectType::ReportReleased) == 1, "停止确认");
        Check(scheduler.IsIdle(), "回到空闲,不累积任何东西");
        Check(scheduler.QueueDepth() == 0, "队列也空了");
        now += 1000;
    }
}

void TestStartFailureDoesNotWedgeTheScheduler() {
    // 起失败不能卡在 Starting:那既不给用户反馈,又占着全产品唯一的名额。
    CreatorRuntimeScheduler scheduler;
    scheduler.Apply(Open(Req("S-1")));
    CreatorRuntimeEvent failed = Ev(CreatorRuntimeEventType::ProcessStartFailed, 2000);
    failed.reason = "Pi Runtime 未配置";
    const auto effects = scheduler.Apply(failed);
    Check(CountEffects(effects, CreatorRuntimeEffectType::ReportRejected) == 1, "报告失败");
    Check(effects[0].detail.find("Pi Runtime 未配置") != std::string::npos, "且带上宿主给的原因");
    Check(scheduler.IsIdle(), "回到空闲,名额让出去了");

    // 队列里的下一个紧接着顶上,而不是也跟着失败。
    scheduler.Apply(Open(Req("S-1")));
    scheduler.Apply(Open(Req("S-2")));
    CreatorRuntimeEvent failed2 = Ev(CreatorRuntimeEventType::ProcessStartFailed, 3000);
    const auto effects2 = scheduler.Apply(failed2);
    Check(scheduler.QueueDepth() == 0, "队列被消费掉了");
    Check(CountEffects(effects2, CreatorRuntimeEffectType::StartProcess) == 1,
          "失败之后队列里的下一个立刻顶上");
    Check(scheduler.ActiveSessionId() == "S-2", "且是排队的那一个");
}

void TestReleaseFailureKeepsTheProcessAlive() {
    // 释放失败时**不能**当它已经释放了。假装成功的后果是下一个请求起来时
    // 上一个进程还活着 —— 那正是"多次开关后进程持续增长"的来路。
    CreatorRuntimeScheduler scheduler;
    scheduler.Apply(Open(Req("S-1")));
    scheduler.Apply(Ev(CreatorRuntimeEventType::ProcessStarted, 2000));
    CreatorRuntimeEvent close = Ev(CreatorRuntimeEventType::CloseRequested, 3000);
    close.sessionId = "S-1";
    scheduler.Apply(close);

    CreatorRuntimeEvent failed = Ev(CreatorRuntimeEventType::ReleaseFailed, 4000);
    const auto effects = scheduler.Apply(failed);
    Check(CountEffects(effects, CreatorRuntimeEffectType::ReportRejected) == 1, "报告释放失败");
    Check(effects[0].detail.find("重试") != std::string::npos, "并要求宿主重试");
    CheckEq(ToString(scheduler.State()), "Releasing", "状态仍是 Releasing,没被改写成空闲");

    // 排队中的请求不会在这个时候启动 —— 那样就会同时有两个进程。
    const auto queued = scheduler.Apply(Open(Req("S-2")));
    Check(CountEffects(queued, CreatorRuntimeEffectType::StartProcess) == 0,
          "释放没确认之前不启动排队的那一个");

    const auto retried = scheduler.Apply(Ev(CreatorRuntimeEventType::ReleaseConfirmed, 5000));
    Check(CountEffects(retried, CreatorRuntimeEffectType::StartProcess) == 1,
          "释放确认之后才启动它");
}

void TestALateStartReportIsIgnoredRatherThanTrusted() {
    // 我们发了 StopProcess 之后,之前那个启动回报才到。据它改状态的话,
    // 一个已经停了进程会被记成 Active —— 而空闲释放就再也不会触发。
    CreatorRuntimeScheduler scheduler;
    scheduler.Apply(Open(Req("S-1")));
    CreatorRuntimeEvent close = Ev(CreatorRuntimeEventType::CloseRequested, 2000);
    close.sessionId = "S-1";
    scheduler.Apply(close);   // Starting -> Releasing
    const auto late = scheduler.Apply(Ev(CreatorRuntimeEventType::ProcessStarted, 3000));
    Check(CountEffects(late, CreatorRuntimeEffectType::ReportRejected) == 1, "迟到回报被拒绝");
    Check(late[0].detail.find("来迟") != std::string::npos, "且说明是来迟了");
    CheckEq(ToString(scheduler.State()), "Releasing", "状态没有被它改写");
}

// ---------------------------------------------------------------------------
// 4. 空闲释放
// ---------------------------------------------------------------------------

void TestIdleReleaseReclaimsTheProcess() {
    CreatorRuntimeScheduler scheduler;
    scheduler.Apply(Open(Req("S-1"), 0));
    scheduler.Apply(Ev(CreatorRuntimeEventType::ProcessStarted, 0));

    Check(!scheduler.IdleDueAt(kCreatorIdleReleaseMs - 1), "还差一点时不释放");
    Check(scheduler.IdleDueAt(kCreatorIdleReleaseMs), "到点就释放");

    const auto effects = scheduler.Tick(kCreatorIdleReleaseMs);
    Check(CountEffects(effects, CreatorRuntimeEffectType::StopProcess) == 1, "到点发出停止");
    CheckEq(ToString(scheduler.State()), "Releasing", "进入 Releasing");

    // 释放成功后回到空闲,草稿仍在盘上(调度不负责删它)。
    const auto done = scheduler.Apply(Ev(CreatorRuntimeEventType::ReleaseConfirmed,
                                         kCreatorIdleReleaseMs + 1));
    Check(CountEffects(done, CreatorRuntimeEffectType::ReportReleased) == 1, "报告已收回");
    Check(scheduler.IsIdle(), "回到空闲");
}

void TestActivityRenewsTheIdleTimer() {
    CreatorRuntimeScheduler scheduler;
    scheduler.Apply(Open(Req("S-1"), 0));
    scheduler.Apply(Ev(CreatorRuntimeEventType::ProcessStarted, 0));

    // 只有当前会话的活动算数。别的会话的活动不能替它续命 —— 否则两个作品会
    // 互相把对方的空闲计时器顶掉,而两者都不释放。
    CreatorRuntimeEvent other = Ev(CreatorRuntimeEventType::TurnStarted, kCreatorIdleReleaseMs - 10);
    other.sessionId = "S-2";
    scheduler.Apply(other);
    Check(scheduler.IdleDueAt(kCreatorIdleReleaseMs), "别的会话的活动不算续命");

    CreatorRuntimeEvent mine = Ev(CreatorRuntimeEventType::TurnStarted, kCreatorIdleReleaseMs - 10);
    mine.sessionId = "S-1";
    scheduler.Apply(mine);
    Check(!scheduler.IdleDueAt(kCreatorIdleReleaseMs), "自己的活动续命了");
    // 续命的含义是计时器从新活动重新起算,所以要到"新活动 + 整个空闲期"才到点。
    // 第一版这里写的是 +10,那只是续命后 10ms,当然没到 —— 断言的是错的,
    // 不是实现错了。
    Check(scheduler.IdleDueAt(kCreatorIdleReleaseMs - 10 + kCreatorIdleReleaseMs),
          "再过一整个空闲期才到点");
}

void TestStartTimeoutReclaimsTheSlot() {
    // 卡在 Starting 比没有更糟:它占着唯一的名额,而用户看不到任何反馈。
    CreatorRuntimeScheduler scheduler;
    scheduler.Apply(Open(Req("S-1"), 0));
    const auto tooEarly = scheduler.Tick(kCreatorStartTimeoutMs - 1);
    Check(tooEarly.empty(), "还差一点时不动作");
    const auto timedOut = scheduler.Tick(kCreatorStartTimeoutMs);
    Check(CountEffects(timedOut, CreatorRuntimeEffectType::ReportRejected) == 1, "超时被报告");
    // 先看规模再索引。调度器出错时这里该报一条失败,不是让整个测试段错误 ——
    // 段错误会把后面每一条断言一起吃掉,而它们本来是绿的。
    Check(!timedOut.empty(), "超时产生了回报");
    if (!timedOut.empty()) {
        Check(timedOut[0].detail.find("超时") != std::string::npos, "且说明是启动超时");
    }
    Check(scheduler.IsIdle(), "名额被收回");
}

// ---------------------------------------------------------------------------
// 5. 重开草稿
// ---------------------------------------------------------------------------

void TestReopeningADraftKeepsTheSameSession() {
    // 重开一份既有草稿必须还是同一个会话、同一个工作区。给它新会话的话,
    // 用户在盘上那一份草稿就再也接不上了 —— 而它以为自己在改同一个作品。
    CreatorRuntimeScheduler scheduler;
    const auto request = Req("S-1");
    scheduler.Apply(Open(request, 1000));
    scheduler.Apply(Ev(CreatorRuntimeEventType::ProcessStarted, 2000));
    Check(scheduler.ActiveSessionId() == "S-1", "起初是 S-1");

    // 关闭,进程收回。
    CreatorRuntimeEvent close = Ev(CreatorRuntimeEventType::CloseRequested, 3000);
    close.sessionId = "S-1";
    scheduler.Apply(close);
    scheduler.Apply(Ev(CreatorRuntimeEventType::ReleaseConfirmed, 4000));
    Check(scheduler.IsIdle(), "空闲了");

    // 重开同一个会话:同一个进程名额,同一个工作区,不多出一个。
    const auto reopened = scheduler.Apply(Open(request, 5000, /*reopen=*/true));
    Check(CountEffects(reopened, CreatorRuntimeEffectType::StartProcess) == 1, "重开会再起进程");
    Check(scheduler.ActiveSessionId() == "S-1", "而且是同一个会话");
    Check(scheduler.QueueDepth() == 0, "没有多出任何排队项");
    const auto* effect = FindEffect(reopened, CreatorRuntimeEffectType::StartProcess);
    Check(effect != nullptr && effect->workspaceRoot == request.workspaceRoot,
          "工作区也还是原来那个");
}

void TestReopeningWhileAlreadyLiveDoesNothing() {
    // 用户已经开着这个草稿,又点了一次"打开":不重复起,也不排队。
    CreatorRuntimeScheduler scheduler;
    const auto request = Req("S-1");
    scheduler.Apply(Open(request, 1000));
    scheduler.Apply(Ev(CreatorRuntimeEventType::ProcessStarted, 2000));
    const auto again = scheduler.Apply(Open(request, 3000, /*reopen=*/true));
    Check(again.empty(), "什么都不做");
    Check(scheduler.QueueDepth() == 0, "也没有排队");
    CheckEq(ToString(scheduler.State()), "Active", "状态不变");
}

void TestTheSameSessionIsNotQueuedTwice() {
    // 连点两下"打开创作"只该排一次。每点一次都排一项的话,
    // 用户看到的是"前面还有 3 个",而那 3 个都是他自己点出来的。
    CreatorRuntimeScheduler scheduler;
    scheduler.Apply(Open(Req("S-1")));
    scheduler.Apply(Ev(CreatorRuntimeEventType::ProcessStarted, 2000));
    scheduler.Apply(Open(Req("S-2"), 3000));
    const auto second = scheduler.Apply(Open(Req("S-2"), 4000));
    Check(CountEffects(second, CreatorRuntimeEffectType::ReportQueued) == 1, "仍然只告诉他在排队");
    Check(scheduler.QueueDepth() == 1, "队列里还是那一项,没有变成两项");
}

// ---------------------------------------------------------------------------
// 6. 排队被消费的顺序
// ---------------------------------------------------------------------------

void TestTheQueueIsConsumedInOrder() {
    CreatorRuntimeScheduler scheduler;
    scheduler.Apply(Open(Req("S-1")));
    scheduler.Apply(Ev(CreatorRuntimeEventType::ProcessStarted, 2000));
    for (const char* session : {"S-2", "S-3", "S-4"}) {
        scheduler.Apply(Open(Req(session)));
    }
    Check(scheduler.QueueDepth() == 3, "排了三个");

    // 排队位置要在回报里说清:第一个人前面没有人,第二个人前面有一个。
    auto queued3 = Open(Req("S-5"));
    const auto report = scheduler.Apply(queued3);
    const auto* queued = FindEffect(report, CreatorRuntimeEffectType::ReportQueued);
    Check(queued != nullptr && queued->queueAhead == 3, "回报里说明前面还有三个");

    CreatorRuntimeEvent close = Ev(CreatorRuntimeEventType::CloseRequested, 3000);
    close.sessionId = "S-1";
    scheduler.Apply(close);
    const auto next = scheduler.Apply(Ev(CreatorRuntimeEventType::ReleaseConfirmed, 4000));
    const auto* started = FindEffect(next, CreatorRuntimeEffectType::StartProcess);
    Check(started != nullptr && started->sessionId == "S-2", "先来的先走");
    Check(scheduler.QueueDepth() == 3, "剩下的还在队列里");
}

// ---------------------------------------------------------------------------
// 7. 状态名的可读性
// ---------------------------------------------------------------------------

void TestStateNamesAreStable() {
    // 这些字符串会出现在日志与诊断里。改名要显式,不要悄悄变成一个别的词。
    const CreatorRuntimeState states[] = {
        CreatorRuntimeState::Idle, CreatorRuntimeState::Starting,
        CreatorRuntimeState::Active, CreatorRuntimeState::Releasing,
    };
    const char* names[] = {"Idle", "Starting", "Active", "Releasing"};
    for (std::size_t i = 0; i < 4; ++i) {
        CheckEq(std::string(ToString(states[i])), names[i], "状态名稳定");
    }
    Check(ToString(CreatorRuntimeState::Idle) != nullptr, "ToString 不返回空");
}

} // namespace
} // namespace miaodesk::creator

int wmain() {
    using namespace miaodesk::creator;
    TestOnlyOneCreationAtATime();
    TestARequestWithoutABindingIsRefused();
    TestCancelOnlyAffectsItsOwnSession();
    TestClosingAQueuedRequestRemovesItFromTheQueue();
    TestRepeatedOpenCloseNeverStacksAProcess();
    TestStartFailureDoesNotWedgeTheScheduler();
    TestReleaseFailureKeepsTheProcessAlive();
    TestALateStartReportIsIgnoredRatherThanTrusted();
    TestIdleReleaseReclaimsTheProcess();
    TestActivityRenewsTheIdleTimer();
    TestStartTimeoutReclaimsTheSlot();
    TestReopeningADraftKeepsTheSameSession();
    TestReopeningWhileAlreadyLiveDoesNothing();
    TestTheSameSessionIsNotQueuedTwice();
    TestTheQueueIsConsumedInOrder();
    TestStateNamesAreStable();

    std::printf("\nCCA-03 creator runtime scheduler: %d checks, %d failures\n", g_checks, g_failures);
    if (g_failures) {
        std::printf("ALL CHECKS FAILED\n");
        return 1;
    }
    std::printf("ALL CHECKS PASSED\n");
    return 0;
}
