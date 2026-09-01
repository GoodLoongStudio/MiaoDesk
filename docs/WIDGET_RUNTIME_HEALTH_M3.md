# MiaoDesk Widget Runtime Health Contract

Status: active implementation contract. The `_M3` filename is retained only for stable existing links.

## Ownership

```text
NativeWidgetHost / WebDesktopSurfaceChild
    ↓ live surface state
DesktopSurfaceTelemetry
    ↓ read-only z-order state
WidgetService
    ↓
WidgetRuntimeHealth / WidgetSurfaceHealth
    ↓
DesktopSnapshot
    ↓
UI / Pi / future Editor
```

UI 和 Pi 不得读取 private INI、枚举 runtime HWND、检查 WebView2 process 或自行解释 sibling ordering。Widget domain 负责把底层 runtime state 翻译成产品健康状态；所有 shell mutation 仍只属于 `DesktopShellHost`。

## Per-surface state

每个 enabled Widget 的 health 应能表达：

- Widget id
- process/running state（适用时）
- surface HWND readiness
- expected parent validity
- `WS_CHILD` validity
- visibility
- monitor/geometry health
- WebView2 Environment/Controller/Navigation readiness（仅 Web Widget）
- z-order reported/valid
- combined rendering health
- stable issue code
- human-readable detail / recommended action

Native Widget 没有 WebView2 lifecycle；不能因为没有 Environment/Controller/Navigation 就被当成降级 surface。

## Health semantics

Enabled 不等于 healthy。

基础 surface 至少需要：

```text
surface exists
expected parent valid
child style valid
visible
monitor valid
geometry valid
z-order valid
```

Web Widget 额外要求 WebView2 lifecycle ready。Native Widget 由其真实 native window/visibility state 表达 rendering readiness。

Aggregate `runtimeHealthy` 只有在 enabled Widget 与 live structured surface 一一对应，并且每个 surface 满足当前 rendering-health contract 时才为 true。

## Stable issue categories

当前产品可以使用稳定 issue category，例如：

```text
surface_missing
process_stopped
parent_invalid
child_style_invalid
surface_hidden
webview_environment_pending
webview_controller_pending
webview_navigation_pending
monitor_topology_unavailable
monitor_missing
geometry_unreported
geometry_mismatch
zorder_unreported
zorder_invalid
runtime_diagnostic_unhealthy
```

UI/Pi 应显示 Widget domain 返回的 issue/action，不自己根据 Win32/WebView2 细节重新推断故障原因。

## Z-order rule

`DesktopSurfaceTelemetry` 只读。它可以报告：

```text
desktop icons above MiaoDesk surfaces
Widget above MiaoDesk wallpaper
```

它不得获得 `SetParent`、`SetWindowPos`、WorkerW discovery 或 repair ownership。

## Validation

仓库不再维护 `MiaoDeskWidgetAcceptance.exe` 或 phase-labelled acceptance PowerShell runner。不要为了替代它们再创建只检查源码 marker 的测试层。

涉及 runtime-health 的改动至少验证：

- formal C++ build/self-test 不回归；
- enabled Widget 与 live surface 一一对应；
- Native Widget 与 Web Widget 使用正确的 readiness 语义；
- invalid parent/style/visibility/monitor/geometry/z-order 能被 health 暴露；
- UI 和 Pi 读取同一个 `DesktopSnapshot`；
- Explorer restart / monitor change 后 health 会恢复到真实状态；
- 在真实 Windows 上目视确认 icons / Widget / wallpaper 层级。

CI 能发现结构和基础 runtime 回归，但不能替代真实桌面视觉验证。
