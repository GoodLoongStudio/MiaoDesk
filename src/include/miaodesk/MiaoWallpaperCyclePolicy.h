#pragma once

// P0-03「Wallpaper 20 次启用/停用/reload 循环」的纯逻辑那一半。
//
// 验收原话:"无错误复活、重复 Surface、Widget 误停用"。这条链的决策全住在
// `WallpaperEngine.cpp` 里,而那个文件为了 `#include "WallpaperEngine.cpp"`
// 自己都不算独立 TU —— 于是循环 20 次要验的"这次切换到底该不该重建运行时"
// 在本机一行都跑不了。
//
// 而它恰恰是四个验收项里三个的源头:
//
//   · **重复 Surface** —— `SetEnabled` 里 `for (attempt = 0; attempt < 2 && !applied; ++attempt)`
//     第二次进来会先 `shellHost_.Refresh()` 再 `AttachToDesktop()`。第一次已经挂上去了、
//     只是 `mountOk_` 因为别的原因为假时,重试会**再挂一层**。要不要重试、重试前要不要
//     先摘旧的,是一个纯状态问题。
//   · **Widget 误停用** —— 停用路径 `StopRuntime()` → `independentHost_.Stop()` →
//     `videoSet_.Stop()`。这三个谁该停、停用了宿主那个"组件还在吗"的判断怎么走,
//     也是状态问题。
//   · **错误复活** —— `if (!applied) { config_.enabled = !targetEnabled; ... }`
//     回滚。回滚之后运行时到底还在不在跑,取决于 applied 为假发生在第几步。
//
// 这里只抽"该不该重建/停止"的裁决,不碰窗口、不碰 Shell、不碰渲染器本机一句不跑。
// 真机上"循环 20 次之后桌面上真的没有重复 Surface"仍要 Windows 签收。
#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace miaodesk::wallpaper_cycle {

// 壁纸运行时的形态。与 WallpaperEngine 的 scene 取值对应(纯字符串,不引 Windows)。
enum class SceneKind {
    Image,
    Video,
    Independent,
};

const wchar_t* SceneKindName(SceneKind kind) noexcept;
// 从配置里的 scene 字段解析。认不出来的按 Image(与 WallpaperEngine 的默认档一致)。
SceneKind ParseSceneKind(std::wstring_view scene) noexcept;

// 一次"启用/停用/reload"请求落在什么状态上。
struct CycleState {
    // 用户要的目标:true=启用,false=停用。
    bool targetEnabled{};
    // 当前配置里的 enabled(已经写盘的那个)。
    bool configEnabled{};
    // 桌面图层挂上了没有(mountOk_)。
    bool mountOk{};
    // 运行时当前是不是活的(RebuildRuntime/StopRuntime 走过之后的现实)。
    bool runtimeActive{};
    SceneKind scene{SceneKind::Image};
};

// 这一刀该往哪切。
enum class CycleAction {
    // 什么都不做:目标与现状已经一致。**这不是"省事",是 P0-03 的正确性** ——
    // 重复执行启用会让 RebuildRuntime 再跑一遍,而它第一句就是 StopRuntime()。
    NoOp,
    // 停:停运行时、隐藏图层、把桌面还给 Explorer。
    Stop,
    // 起:挂载、建运行时、显示。
    Start,
    // 重建:先停再起。reload 走这条。
    Rebuild,
};

const wchar_t* CycleActionName(CycleAction action) noexcept;

struct CycleDecision {
    CycleAction action{CycleAction::NoOp};
    // 这次切换要不要碰 Shell(AttachToDesktop / Refresh)。纯裁决,给日志与诊断用。
    bool touchesShell{};
    // 给人看的一句话:这次为什么这么做。空表示 NoOp。
    std::wstring reason;
};

// 判一次:这个请求该做什么。
//
// 关键不变量(全部由 CycleTest 钉住):
//   · 目标 == configEnabled 且运行时已就位 → NoOp,**不许**再走一遍 Start/Rebuild;
//   · 目标 == configEnabled 但运行时没起来 → Rebuild(状态写对了而现实没跟上,
//     这正是"错误复活"要修的那一半);
//   · 目标 != configEnabled → 按目标 Start 或 Stop。
CycleDecision DecideCycleAction(const CycleState& state) noexcept;

// 重试策略:`SetEnabled` 的 `for (attempt = 0; attempt < 2 && !applied; ++attempt)`。
//
// 抽出来是因为"第二次尝试前要不要先摘掉第一次挂上去的那层"是一个纯规则,
// 而它直接决定 P0-03 的"无重复 Surface"。
struct RetryPolicy {
    // 最多尝试几次(与 WallpaperEngine 的 2 一致)。
    std::uint8_t maxAttempts{2};
    // 重试前要不要先摘掉已挂载的图层。**必须为 true** —— 否则第一次挂成功、
    // 只有 mountOk_ 因别的原因为假时,第二次会再挂一层,桌面上就是重复 Surface。
    bool detachBeforeRetry{true};
};

// 第 attempt 次(从 0 起)尝试前要不要先摘层。
// attempt == 0 返回 false(第一次本来就还没挂)。
bool ShouldDetachBeforeAttempt(const RetryPolicy& policy, std::uint8_t attempt) noexcept;

} // namespace miaodesk::wallpaper_cycle
