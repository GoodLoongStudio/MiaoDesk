#pragma once

// CAP-05:组件数据与动作的公共契约。
//
// 现状的三个 Provider(时间、天气、待办)是三个互不相同的结构体:一个有 generation、
// 一个有 valid + status + observedTime、一个有 valid + generation,谁都没有 loading 态,
// 谁都不处理"权限被撤销"和"数据过期"。结果是官方、用户和 AI 三方各自面对三种语义,
// 而"组件显示的是 20 分钟前的天气"这件事没有任何一处能表达。
//
// 动作侧更空:没有注册表、没有 dispatch 表、没有 action id,唯一一个"完成待办"是
// DesktopControlService 上的一个普通成员函数,只有管理界面调它。组件 Surface 上
// 点一个复选框 -> 完成待办 这条最自然的路径不存在。
//
// 这份契约先把**形状**定下来,并且是纯逻辑:快照要有时间戳、要有有效期、要能说清
// 自己是加载中/可用/空/离线/失败/已撤销;动作要有 id、要有参数、要有结果、要能取消、
// 要幂等。有了形状,官方组件、用户包和 AI 创作才可能共用同一套;在那之前,任何
// "Provider/Action 可用"的说法都只是名字。
//
// 它不碰盘、不 import Windows 头,于是这些判定在本机就能真验。
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "miaodesk/MiaoContentDataBinding.h"

namespace miaodesk::desktop {

// 一份数据快照处于什么状态。**空与离线不是一回事**:空是"问过了,答案是什么都没有",
// 离线是"没问上"。把离线显示成空,用户会以为自己的日程真的空了。
enum class ProviderState {
    Loading,     // 正在取,还没有可用内容
    Available,   // 有内容
    Empty,       // 问过了,确实没有内容
    Offline,     // 网络/服务不可用
    Failed,      // 取到了,但内容坏到不能用
    Revoked,     // 权限被撤销:不是没有数据,是不再允许看
};

const char* ToString(ProviderState state) noexcept;

// 这份状态算不算"可以把内容显示给用户"。Loading / Offline / Failed / Revoked 都不算,
// 而它们各自的显示文案不同 —— 合成一个"无数据"会同时骗到四类用户。
bool ProviderStateShowsContent(ProviderState state) noexcept;

struct ProviderSnapshot {
    std::wstring providerId;             // 稳定 id,例如 "tasks" / "weather"
    std::uint64_t generation{};          // 每次内容变化递增;相同内容不得递增
    std::uint64_t observedAtUnixMs{};    // 数据是什么时刻观察到的(不是收到时刻)
    std::uint64_t validForMs{};          // 有效期;0 表示永不过期
    ProviderState state{ProviderState::Loading};
    std::wstring statusText;             // 给用户看的一句话;非 Available 时必须非空
    content::ContentDataValues values;

    // 过没过期。**没有时间戳就不能判断新鲜**,返回 false —— 假装新鲜比说不可信更坏:
    // 组件会把一份不知道多久以前的数据当最新数据显示。
    bool IsFresh(std::uint64_t nowUnixMs) const noexcept {
        return observedAtUnixMs != 0 && (validForMs == 0 || nowUnixMs <= observedAtUnixMs + validForMs);
    }

    // 能不能被内容读取。过期与不可用都不行,而原因必须能从 state 读出来。
    bool UsableAt(std::uint64_t nowUnixMs) const noexcept {
        return ProviderStateShowsContent(state) && IsFresh(nowUnixMs);
    }
};

// 对一份快照的判定。判据说的是"现在该显示什么",而不是"曾经是什么"。
struct ProviderSnapshotVerdict {
    bool usable{};
    bool stale{};        // 有内容但过期了
    ProviderState state{ProviderState::Loading};
    std::wstring reason; // 不能用时说清为什么;能用时为空
};

ProviderSnapshotVerdict JudgeProviderSnapshot(const ProviderSnapshot& snapshot,
                                             std::uint64_t nowUnixMs);

// ---------------------------------------------------------------------------
// 动作
// ---------------------------------------------------------------------------

// 一个动作的声明。id 是稳定名,宿主按它找实现;参数按名字取,不按位置。
struct WidgetActionDefinition {
    std::wstring id;                  // 例如 "tasks.complete"
    std::wstring targetProviderId;    // 这个动作落在哪个数据源上(没有则为空)
    std::wstring summary;             // 给人看的一句话
    bool requiresWritePermission{};   // 读写权限分开声明
    bool idempotent{};                // 同一个 operationId 重复执行结果是否相同
};

// 一次动作请求。operationId 由调用方生成,是幂等的凭据。
struct WidgetActionRequest {
    std::wstring actionId;
    std::wstring targetId;            // 哪个实例/哪一条数据
    std::wstring operationId;         // 幂等凭据;同一个 id 重复请求必须得到同一个结果
    std::vector<std::pair<std::wstring, std::wstring>> args;
};

struct WidgetActionResult {
    bool accepted{};      // 请求合法且被受理
    bool succeeded{};     // 落地成功
    std::wstring code;    // 结构化原因:UnknownAction / MissingArgument / Revoked /
                          // Cancelled / DuplicateOperation / Failed ...
    std::wstring message; // 给用户看的一句话
    bool replayed{};      // 这一次是重复 operationId 的回放,不是新执行
};

// 受控的动作派发。它是注册表的唯一入口 —— 直接调 TodayTaskStore 的路径不算
// "组件能完成待办",那只算管理界面能。
class WidgetActionRegistry {
public:
    void Register(WidgetActionDefinition definition);

    const WidgetActionDefinition* Find(std::wstring_view id) const noexcept;

    const std::vector<WidgetActionDefinition>& Definitions() const noexcept { return definitions_; }

    // 执行。handler 返回是否落地成功;空 handler 表示这个动作没有实现。
    //
    // 三个必须由注册表保证的性质:
    //   · 未知动作被明确拒绝,不是"什么也没发生";
    //   · 同一个 operationId 重复请求返回**同一个结果**,并且标记 replayed ——
    //     重复点击不得产生第二次副作用,也不得让用户以为失败了;
    //   · 已撤销(permissionRevoked)的动作被拒绝,而不是继续执行。
    WidgetActionResult Dispatch(const WidgetActionRequest& request,
                                const std::function<bool(const WidgetActionRequest&)>& handler,
                                bool permissionRevoked = false) const;

    // 供测试与 UI:某个 operationId 是否已经执行过。
    bool HasExecuted(std::wstring_view operationId) const noexcept;

    // 记录一次已执行的结果(用于跨会话恢复:重启后同一个迟到请求仍应得到同一答案)。
    void Remember(std::wstring_view operationId, WidgetActionResult result);

    void Forget(std::wstring_view operationId);

    std::size_t ExecutedCount() const noexcept { return results_.size(); }

private:
    std::vector<WidgetActionDefinition> definitions_;
    // 已执行的凭据 -> 结果。mutable 是因为它是**幂等凭据的账本**,不是注册表的内容:
    // Dispatch 因此可以保持 const(调用方手里常常只有 const 引用),而"同一个
    // operationId 第二次来"仍然能拿到第一次的结果。把它当成可变状态挪出 const,
    // 结果是每个调用点都得持有一份非 const 副本,而那正是需要防重的地方。
    mutable std::vector<std::pair<std::wstring, WidgetActionResult>> results_;
};

} // namespace miaodesk::desktop
