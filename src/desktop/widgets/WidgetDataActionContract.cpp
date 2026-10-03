#include "miaodesk/WidgetDataActionContract.h"

#include <utility>

namespace miaodesk::desktop {
namespace {

// 一份"有效但过期"的快照:内容在,时间戳老。这是最容易骗到用户的一类 ——
// 组件把 40 分钟前的天气当"现在 23°"显示,而数据来源没有任何变化。
ProviderSnapshot StaleWeather() {
    ProviderSnapshot snapshot;
    snapshot.providerId = L"weather";
    snapshot.generation = 4;
    snapshot.observedAtUnixMs = 1000;
    snapshot.validForMs = 60000;
    snapshot.state = ProviderState::Available;
    snapshot.statusText = L"晴,23°";
    snapshot.values.emplace(L"weather.temperature", content::PropertyValue{static_cast<std::int64_t>(23)});
    return snapshot;
}

} // namespace

const char* ToString(ProviderState state) noexcept {
    switch (state) {
    case ProviderState::Loading: return "Loading";
    case ProviderState::Available: return "Available";
    case ProviderState::Empty: return "Empty";
    case ProviderState::Offline: return "Offline";
    case ProviderState::Failed: return "Failed";
    case ProviderState::Revoked: return "Revoked";
    }
    return "Unknown";
}

bool ProviderStateShowsContent(ProviderState state) noexcept {
    return state == ProviderState::Available || state == ProviderState::Empty;
}

ProviderSnapshotVerdict JudgeProviderSnapshot(const ProviderSnapshot& snapshot,
                                             std::uint64_t nowUnixMs) {
    ProviderSnapshotVerdict verdict;
    verdict.state = snapshot.state;
    if (!ProviderStateShowsContent(snapshot.state)) {
        verdict.usable = false;
        verdict.reason = snapshot.statusText.empty() ? std::wstring(L"数据当前不可用。")
                                                    : snapshot.statusText;
        return verdict;
    }
    if (!snapshot.IsFresh(nowUnixMs)) {
        // 有过期内容也是不可用,但必须与"从来没有内容"分开说 ——
        // "显示的是旧数据"和"没有数据"对用户是两个完全不同的信息。
        verdict.stale = true;
        verdict.reason = L"这份数据已过期,不能当当前状态显示。";
        return verdict;
    }
    verdict.usable = true;
    return verdict;
}

// ---------------------------------------------------------------------------
// 动作注册表
// ---------------------------------------------------------------------------

void WidgetActionRegistry::Register(WidgetActionDefinition definition) {
    if (definition.id.empty()) return;
    for (auto& existing : definitions_) {
        if (existing.id == definition.id) {
            existing = std::move(definition);
            return;
        }
    }
    definitions_.push_back(std::move(definition));
}

const WidgetActionDefinition* WidgetActionRegistry::Find(std::wstring_view id) const noexcept {
    for (const auto& definition : definitions_) {
        if (definition.id == id) return &definition;
    }
    return nullptr;
}

WidgetActionResult WidgetActionRegistry::Dispatch(
    const WidgetActionRequest& request,
    const std::function<bool(const WidgetActionRequest&)>& handler,
    bool permissionRevoked) const {
    WidgetActionResult result;
    const auto* definition = Find(request.actionId);
    if (!definition) {
        // 未知动作必须明确拒绝。"什么也没发生"是最坏的答案:用户点了一个控件,
        // 没有任何反馈,而他无法区分"没点到""坏了"和"不支持"。
        result.code = L"UnknownAction";
        result.message = L"宿主不认识这个动作:" + request.actionId;
        return result;
    }
    if (request.operationId.empty()) {
        // 没有幂等凭据就无法防重。拒绝而不是放行:放行意味着重复点击会执行两次。
        result.code = L"MissingOperationId";
        result.message = L"动作请求缺少 operationId,无法保证重复执行只生效一次。";
        return result;
    }
    if (permissionRevoked) {
        result.code = L"Revoked";
        result.message = L"这个动作需要的权限已被撤销。";
        return result;
    }

    for (const auto& [id, remembered] : results_) {
        if (id != request.operationId) continue;
        // 同一个凭据再来一次:回放同一个结果,并告诉调用方这是回放。
        WidgetActionResult replayed = remembered;
        replayed.replayed = true;
        return replayed;
    }

    if (!handler) {
        result.code = L"NotImplemented";
        result.message = L"这个动作当前构建里没有实现。";
        return result;
    }
    result.accepted = true;
    result.succeeded = handler(request);
    if (!result.succeeded) {
        result.code = L"Failed";
        result.message = L"动作没有成功,原因见宿主日志。";
        return result;
    }
    result.code = L"None";
    result.message = L"已完成。";
    results_.emplace_back(request.operationId, result);
    return result;
}

bool WidgetActionRegistry::HasExecuted(std::wstring_view operationId) const noexcept {
    for (const auto& [id, remembered] : results_) {
        if (id == operationId) return true;
    }
    return false;
}

void WidgetActionRegistry::Remember(std::wstring_view operationId, WidgetActionResult result) {
    for (auto& [id, remembered] : results_) {
        if (id != operationId) continue;
        remembered = std::move(result);
        return;
    }
    results_.emplace_back(std::wstring(operationId), std::move(result));
}

void WidgetActionRegistry::Forget(std::wstring_view operationId) {
    for (auto it = results_.begin(); it != results_.end(); ++it) {
        if (it->first != operationId) continue;
        results_.erase(it);
        return;
    }
}

} // namespace miaodesk::desktop
