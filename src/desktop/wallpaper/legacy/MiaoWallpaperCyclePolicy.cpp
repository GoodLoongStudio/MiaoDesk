#include "miaodesk/MiaoWallpaperCyclePolicy.h"

#include <cwctype>

namespace miaodesk::wallpaper_cycle {

const wchar_t* SceneKindName(SceneKind kind) noexcept {
    switch (kind) {
    case SceneKind::Image: return L"Image";
    case SceneKind::Video: return L"Video";
    case SceneKind::Independent: break;
    }
    return L"Independent";
}

SceneKind ParseSceneKind(std::wstring_view scene) noexcept {
    // 大小写不敏感,与 WallpaperEngine 里对 config_.scene 的其它比较口径一致。
    std::wstring lowered(scene);
    for (auto& ch : lowered) ch = static_cast<wchar_t>(std::towlower(ch));
    if (lowered == L"video") return SceneKind::Video;
    if (lowered == L"independent" || lowered == L"per-monitor") return SceneKind::Independent;
    return SceneKind::Image;
}

const wchar_t* CycleActionName(CycleAction action) noexcept {
    switch (action) {
    case CycleAction::NoOp: return L"NoOp";
    case CycleAction::Stop: return L"Stop";
    case CycleAction::Start: return L"Start";
    case CycleAction::Rebuild: break;
    }
    return L"Rebuild";
}

CycleDecision DecideCycleAction(const CycleState& state) noexcept {
    CycleDecision decision;

    if (state.targetEnabled != state.configEnabled) {
        // 目标与已写盘的配置不一致 —— 按目标切。
        if (state.targetEnabled) {
            decision.action = CycleAction::Start;
            decision.touchesShell = true;
            decision.reason = L"目标为启用而当前是停用：挂载桌面图层并启动运行时";
        } else {
            decision.action = CycleAction::Stop;
            decision.touchesShell = true;
            decision.reason = L"目标为停用而当前是启用：停运行时、隐藏图层并让桌面重绘";
        }
        return decision;
    }

    // 目标与配置一致 —— 分两种现实。
    //
    // "运行时是活的"**必须以图层挂着为前提**:图层没挂上而 runtimeActive 仍为真,
    // 说明运行时是上一轮残留的(停用路径只隐藏图层、没有走到 StopRuntime 的那一类
    // 失败路径),这时候按 NoOp 处理就是 P0-03 的"错误复活"—— 桌面上一片空白,
    // 而状态说一切正常。所以这里判的是两者**同时**成立。
    if (state.runtimeActive && state.mountOk) {
        // 现实也跟上了。**这是 NoOp**,不是"再走一遍保险"。
        //
        // 上一版这里没有这个早退:重复的启用请求会一路走到 RebuildRuntime(),
        // 而 RebuildRuntime 第一句就是 StopRuntime()。循环 20 次里每一次多余的启用
        // 都在停一次再起一次 —— 桌面上就是 P0-03 验收里那个"重复 Surface"。
        decision.action = CycleAction::NoOp;
        decision.reason = L"目标与现状已一致，且运行时是活的：忽略重复请求";
        return decision;
    }

    // 配置说该是这样,而现实没跟上(mountOk_ 为假、或运行时被性能策略停掉了)。
    // 这一支是"错误复活"的另一半:状态写对了,桌面没恢复。
    decision.action = CycleAction::Rebuild;
    decision.touchesShell = !state.mountOk;
    decision.reason = state.mountOk
                          ? L"配置与目标一致但运行时没在跑：重建运行时"
                          : L"配置与目标一致但桌面图层没挂上：重新挂载并重建运行时";
    return decision;
}

bool ShouldDetachBeforeAttempt(const RetryPolicy& policy, std::uint8_t attempt) noexcept {
    if (attempt == 0) return false;         // 第一次本来就没挂
    if (attempt >= policy.maxAttempts) return false;
    // 只有策略要求时才摘。默认策略要求 —— 见结构体注释。
    return policy.detachBeforeRetry;
}

} // namespace miaodesk::wallpaper_cycle
