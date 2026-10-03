// CAP-05:组件数据与动作的公共契约。
//
// 现状是三个 Provider 三种结构、零个动作注册表。这份测试钉的是契约的**判定规则**,
// 尤其是三条最容易被"看起来能用"糊过去的:
//   · 没有时间戳的快照不能算新鲜 —— 假装新鲜比说不可信更坏,组件会把不知道多久以前的
//     数据当"现在"显示;
//   · 空与离线不是一回事 —— 把离线显示成空,用户会以为自己的日程真的空了;
//   · 同一个 operationId 重复请求必须得到同一个结果并标记为回放 ——
//     重复点击不得产生第二次副作用,也不得让用户以为失败了。
//
// 全部在本机实跑:契约是纯逻辑,不需要 Windows。
#include "miaodesk/WidgetDataActionContract.h"

#include <cstdio>
#include <string>

namespace miaodesk::desktop {
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

ProviderSnapshot Fresh() {
    ProviderSnapshot snapshot;
    snapshot.providerId = L"tasks";
    snapshot.generation = 7;
    snapshot.observedAtUnixMs = 100000;
    snapshot.validForMs = 60000;
    snapshot.state = ProviderState::Available;
    snapshot.values.emplace(L"tasks.total", content::PropertyValue{std::int64_t(3)});
    return snapshot;
}

WidgetActionDefinition CompleteTask() {
    WidgetActionDefinition definition;
    definition.id = L"tasks.complete";
    definition.targetProviderId = L"tasks";
    definition.summary = L"完成一条待办";
    definition.requiresWritePermission = true;
    definition.idempotent = true;
    return definition;
}

WidgetActionRequest Complete(std::wstring operationId) {
    WidgetActionRequest request;
    request.actionId = L"tasks.complete";
    request.targetId = L"t1";
    request.operationId = std::move(operationId);
    return request;
}

} // namespace
} // namespace miaodesk::desktop

int wmain() {
    using namespace miaodesk::desktop;

    // ---- 1. 快照判定 ----
    const auto fresh = Fresh();
    Check(JudgeProviderSnapshot(fresh, 100000).usable, "有效期内的快照可用。");
    Check(JudgeProviderSnapshot(fresh, 160000).usable, "刚好在有效期边界上仍可用。");
    const auto stale = JudgeProviderSnapshot(fresh, 100000 + 60000 + 1);
    Check(!stale.usable, "过期内容不可用。");
    Check(stale.stale, "过期要被单独标记 —— 『显示旧数据』和『没有数据』是两个信息。");

    // 没有时间戳就不能判断新鲜。返回 false,而不是假设它新鲜。
    auto timeless = Fresh();
    timeless.observedAtUnixMs = 0;
    const auto timelessVerdict = JudgeProviderSnapshot(timeless, 100000);
    Check(!timelessVerdict.usable, "没有观察时间的快照不算可用。");
    Check(timelessVerdict.stale, "没有时间戳按过期处理,而不是按新鲜处理。");

    // 四种不可用状态各自可辨,不许合成一个"无数据"。
    for (const auto state : {ProviderState::Loading, ProviderState::Offline,
                             ProviderState::Failed, ProviderState::Revoked}) {
        auto snapshot = Fresh();
        snapshot.state = state;
        const auto verdict = JudgeProviderSnapshot(snapshot, 100000);
        Check(!verdict.usable, std::string("状态 ") + ToString(state) + " 不可用。");
        Check(!verdict.reason.empty(), std::string("状态 ") + ToString(state) + " 必须带一句说明。");
    }
    auto empty = Fresh();
    empty.state = ProviderState::Empty;
    empty.values.clear();
    Check(JudgeProviderSnapshot(empty, 100000).usable,
          "空是可用状态 —— 它要显示『还没有内容』,而不是报错。");
    Check(!ProviderStateShowsContent(ProviderState::Revoked), "撤销后不得再显示内容。");
    Check(ProviderStateShowsContent(ProviderState::Empty), "空仍算有内容要显示。");

    // ---- 2. 动作注册表 ----
    WidgetActionRegistry registry;
    registry.Register(CompleteTask());
    Check(registry.Find(L"tasks.complete") != nullptr, "注册过的动作查得到。");
    Check(registry.Find(L"tasks.notThere") == nullptr, "没注册的动作查不到。");
    Check(registry.Definitions().size() == 1, "定义表里就一条。");

    auto calls = 0;
    const auto handler = [&calls](const WidgetActionRequest&) {
        ++calls;
        return true;
    };

    // 未知动作:明确拒绝。
    WidgetActionRequest unknown;
    unknown.actionId = L"tasks.deleteEverything";
    unknown.operationId = L"op-unknown";
    const auto unknownResult = registry.Dispatch(unknown, handler);
    Check(!unknownResult.accepted, "未知动作不被受理。");
    Check(unknownResult.code == L"UnknownAction", "未知动作的代码是 UnknownAction。");
    Check(calls == 0, "未知动作不会执行到 handler。");

    // 缺幂等凭据:拒绝。放行意味着重复点击会执行两次。
    WidgetActionRequest noOperation;
    noOperation.actionId = L"tasks.complete";
    const auto noOperationResult = registry.Dispatch(noOperation, handler);
    Check(!noOperationResult.accepted, "没有 operationId 的请求被拒绝。");
    Check(calls == 0, "没有凭据不会执行。");

    // 权限撤销:拒绝,而不是继续执行。
    const auto revoked = registry.Dispatch(Complete(L"op-revoked"), handler, /*permissionRevoked=*/true);
    Check(!revoked.accepted, "权限撤销后动作被拒绝。");
    Check(revoked.code == L"Revoked", "撤销有它自己的代码。");
    Check(calls == 0, "撤销后没有执行。");

    // 正常执行一次。
    const auto first = registry.Dispatch(Complete(L"op-1"), handler);
    Check(first.accepted && first.succeeded, "第一次执行成功。");
    Check(!first.replayed, "第一次不是回放。");
    Check(calls == 1, "handler 只被调用一次。");

    // 同一个凭据再来:回放同一个结果,不再执行。
    const auto replay = registry.Dispatch(Complete(L"op-1"), handler);
    Check(replay.accepted && replay.succeeded, "重复请求得到同样的成功结果。");
    Check(replay.replayed, "重复请求标记为回放 —— 调用方要知道这不是新执行。");
    Check(calls == 1, "重复点击没有产生第二次副作用。");

    // 没有实现的动作:说清没有实现,而不是失败得像一次错误。
    WidgetActionDefinition pending;
    pending.id = L"media.playPause";
    pending.summary = L"播放/暂停";
    registry.Register(pending);
    const auto notImplemented = registry.Dispatch(Complete(L"op-media"), nullptr);
    Check(!notImplemented.succeeded, "没有 handler 的动作不成功。");
    Check(notImplemented.code == L"NotImplemented",
          "代码是 NotImplemented —— 与『执行了但失败』分开,重试不会有变化。");

    // handler 返回失败:结果记不下来(下一次还应重试),且代码可辨。
    auto failing = 0;
    const auto failHandler = [&failing](const WidgetActionRequest&) {
        ++failing;
        return false;
    };
    const auto failed = registry.Dispatch(Complete(L"op-fail"), failHandler);
    Check(!failed.succeeded && failed.code == L"Failed", "失败的动作用于 Failed。");
    Check(!registry.HasExecuted(L"op-fail"),
          "失败不记入已执行 —— 否则同一个凭据再也重试不了,而它本该可以。");

    // 恢复:重启后同一个迟到请求仍应得到同一答案。
    WidgetActionRegistry restored;
    restored.Register(CompleteTask());
    restored.Remember(L"op-1", first);
    Check(restored.HasExecuted(L"op-1"), "跨会话记住凭据。");
    const auto late = restored.Dispatch(Complete(L"op-1"), handler);
    Check(late.succeeded && late.replayed, "重启后迟到的重复请求仍得到同一结果。");
    Check(calls == 1, "并且没有重新执行。");

    if (g_failures != 0) {
        std::printf("\n失败 %d / %d\n", g_failures, g_checks);
        return 1;
    }
    std::printf("组件数据与动作契约:全部 %d 项检查通过\n", g_checks);
    return 0;
}
