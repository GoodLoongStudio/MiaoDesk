// P0-03「Wallpaper 20 次启用/停用/reload 循环」的纯逻辑裁决。
//
// 这个文件钉住"这次切换到底该不该重建运行时",以及"重试前要不要先摘层"。
// 它们在 `WallpaperEngine.cpp` 里,而那个文件自己都不是独立 TU(被
// `WallpaperEngineProduction.cpp` `#include`),于是 P0-03 循环 20 次要验的这几条
// 不变量在本机一行都跑不了。
//
// 反空洞自检:一个恒返回 Rebuild 的裁决在每个断言上同样全绿,所以先喂一个
// 明知该 NoOp 的状态、一个明知该 Rebuild 的状态,确认裁决真的会动。
#include "miaodesk/MiaoWallpaperCyclePolicy.h"

#include <cstdio>
#include <string>

namespace miaodesk::wallpaper_cycle {
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

bool VerdictStillMoves() {
    CycleState noop;
    noop.targetEnabled = true;
    noop.configEnabled = true;
    noop.mountOk = true;
    noop.runtimeActive = true;
    CycleState rebuild = noop;
    rebuild.runtimeActive = false;
    return DecideCycleAction(noop).action == CycleAction::NoOp &&
           DecideCycleAction(rebuild).action == CycleAction::Rebuild;
}

} // namespace
} // namespace miaodesk::wallpaper_cycle

int wmain() {
    using namespace miaodesk;
    using namespace miaodesk::wallpaper_cycle;

    if (!VerdictStillMoves()) {
        std::printf("\n[FAIL] 反空洞自检没通过:裁决恒返回同一个动作\n");
        return 1;
    }
    std::printf("  [PASS] 反空洞自检:已就位判 NoOp、运行时没起来判 Rebuild\n");
    ++g_checks;

    CycleState base;
    base.targetEnabled = true;
    base.configEnabled = true;
    base.mountOk = true;
    base.runtimeActive = true;

    // ---- 1. 重复请求必须是 NoOp(P0-03「无重复 Surface」)----
    {
        const auto d = DecideCycleAction(base);
        Check(d.action == CycleAction::NoOp, "目标、配置、运行时三者一致 → NoOp");
        Check(!d.touchesShell, "NoOp 不碰 Shell(不再 AttachToDesktop)");
        Check(d.reason.find(L"忽略重复请求") != std::wstring::npos, "原因说清是忽略重复请求");
    }

    // ---- 2. 状态对了而现实没跟上 → Rebuild(P0-03「无错误复活」的另一半)----
    {
        CycleState mountLost = base;
        mountLost.mountOk = false;
        const auto d = DecideCycleAction(mountLost);
        Check(d.action == CycleAction::Rebuild, "配置一致但图层没挂上 → Rebuild");
        Check(d.touchesShell, "图层没挂上时要碰 Shell(重新挂载)");
        Check(d.reason.find(L"重新挂载") != std::wstring::npos, "原因说清要重新挂载");

        CycleState runtimeGone = base;
        runtimeGone.runtimeActive = false;
        const auto d2 = DecideCycleAction(runtimeGone);
        Check(d2.action == CycleAction::Rebuild, "配置一致但运行时没跑 → Rebuild");
        Check(!d2.touchesShell, "图层还在就只重建运行时,不重新挂载");
        Check(d2.reason.find(L"重建运行时") != std::wstring::npos, "原因说清是重建运行时");
    }

    // ---- 3. 目标与配置不一致 → 按目标 Start / Stop ----
    {
        CycleState toStart = base;
        toStart.targetEnabled = true;
        toStart.configEnabled = false;
        toStart.runtimeActive = false;
        const auto d = DecideCycleAction(toStart);
        Check(d.action == CycleAction::Start, "配置是停用、目标启用 → Start");
        Check(d.touchesShell, "Start 要挂载图层");

        CycleState toStop = base;
        toStop.targetEnabled = false;
        toStop.configEnabled = true;
        const auto d2 = DecideCycleAction(toStop);
        Check(d2.action == CycleAction::Stop, "配置是启用、目标停用 → Stop");
        Check(d2.touchesShell, "Stop 要让桌面重绘");

        // "配置说停用、目标也停用"是 2 的反面:别因为 runtimeActive 假就去 Rebuild。
        CycleState alreadyStopped = toStop;
        alreadyStopped.runtimeActive = false;
        Check(DecideCycleAction(alreadyStopped).action == CycleAction::Stop,
              "配置与目标都停用 → Stop(不是 Rebuild:没有要重建的东西)");
    }

    // ---- 4. 重试前必须摘层(P0-03「无重复 Surface」)----
    {
        const RetryPolicy policy;
        Check(policy.maxAttempts == 2, "最多尝试 2 次(与 WallpaperEngine 的循环一致)");
        Check(policy.detachBeforeRetry, "默认策略要求重试前摘层");
        Check(!ShouldDetachBeforeAttempt(policy, 0), "第 0 次不摘(本来就还没挂)");
        Check(ShouldDetachBeforeAttempt(policy, 1), "第 1 次之前必须摘 —— 否则第一次挂成功、"
                                                    "mountOk_ 因别的原因为假时,第二次会再挂一层");
        Check(!ShouldDetachBeforeAttempt(policy, 2), "超出尝试次数就不再摘");

        RetryPolicy careless = policy;
        careless.detachBeforeRetry = false;
        Check(!ShouldDetachBeforeAttempt(careless, 1),
              "策略明确说不用摘时才不摘 —— 那条路径上重复 Surface 的风险由调用方承担");

        // 边界:0 次尝试的策略下什么都不该发生(配错了也不许 silently 挂两次)。
        RetryPolicy none;
        none.maxAttempts = 0;
        Check(!ShouldDetachBeforeAttempt(none, 0), "0 次尝试时第 0 次也不摘");
        Check(!ShouldDetachBeforeAttempt(none, 1), "0 次尝试时第 1 次也不摘");
    }

    // ---- 5. scene 解析(大小写与别名的口径)----
    {
        Check(ParseSceneKind(L"video") == SceneKind::Video, "认 video");
        Check(ParseSceneKind(L"VIDEO") == SceneKind::Video, "大写也算");
        Check(ParseSceneKind(L"independent") == SceneKind::Independent, "认 independent");
        Check(ParseSceneKind(L"per-monitor") == SceneKind::Independent, "per-monitor 是别名");
        Check(ParseSceneKind(L"") == SceneKind::Image, "空按 Image(默认档)");
        Check(ParseSceneKind(L"完全不认识") == SceneKind::Image, "认不出来的按 Image,不抛");
        Check(ParseSceneKind(L"image") == SceneKind::Image, "认 image");
    }

    // ---- 6. 名字都不是空的 ----
    {
        for (const auto action : {CycleAction::NoOp, CycleAction::Stop, CycleAction::Start,
                                  CycleAction::Rebuild}) {
            Check(CycleActionName(action) != nullptr && CycleActionName(action)[0] != L'\0',
                  "每个动作都有非空名字(日志要能印出来)");
        }
        for (const auto kind : {SceneKind::Image, SceneKind::Video, SceneKind::Independent}) {
            Check(SceneKindName(kind) != nullptr && SceneKindName(kind)[0] != L'\0',
                  "每种场景都有非空名字");
        }
    }

    if (g_failures != 0) {
        std::printf("\n失败 %d / %d\n", g_failures, g_checks);
        return 1;
    }
    std::printf("\n壁纸循环裁决:全部 %d 项通过\n", g_checks);
    return 0;
}
