# MiaoDesk Widget Placement Health Contract

Status: active runtime contract. The `_M3` filename is retained only for stable existing links.

Widget 进程/窗口活着并不代表 placement healthy。目标 monitor 错误、geometry 漂移或 display reconnect 后落到错误位置都属于真实故障。

## Ownership

Placement health 由 `WidgetService` 产生，并通过 `WidgetSurfaceHealth` / `WidgetRuntimeHealth` 进入 `DesktopSnapshot`。

UI、Pi 和其他 client 只消费这个状态；它们不得自行枚举/移动 desktop attachment HWND。`DesktopShellHost` 仍是 Windows desktop attachment/z-order mutation 的唯一 owner。

## Per-surface placement state

每个 enabled Widget 应能报告：

```text
monitorId
monitorReported
monitorValid
geometryReported
geometryValid
expectedLeft / expectedTop / expectedRight / expectedBottom
actualLeft / actualTop / actualRight / actualBottom
```

Expected rectangle 来自 persisted normalized Widget rect + 当前解析出的 target monitor。空 monitor id 可以解析到当前 primary monitor；非空 id 必须通过稳定 monitor-id contract 解析。

Actual rectangle 来自 live surface。`geometryValid` 应允许小范围 Win32 rounding tolerance，但不能把明显的位置/尺寸错误当作 healthy。

## Health rule

Placement 进入 rendering health 前至少要求：

```text
monitor topology reported
target monitor resolved
live geometry reported
live geometry matches expected rectangle within tolerance
```

常用稳定 issue category：

```text
monitor_topology_unavailable
monitor_missing
geometry_unreported
geometry_mismatch
```

Issue/action 由 Widget domain 生成，UI/Pi 不重复实现 monitor/HWND remediation 逻辑。

## Display changes

Display disconnect/reconnect、primary monitor change、DPI/topology change 后：

- persisted Widget identity 保持；
- target monitor 重新解析；
- live geometry 重新计算/恢复；
- health 必须反映恢复后的真实 surface；
- 如果目标 monitor 不再存在，应明确报告 invalid state，而不是静默宣称 healthy。

## Validation

仓库不再维护独立 phase acceptance runner。涉及 placement 的改动需要在真实 Windows 上验证：

1. 新建 Widget 初始位置有效且不重叠；
2. drag 后 normalized position 持久化；
3. 重启后回到相同逻辑位置；
4. 多显示器下 target monitor 正确；
5. monitor disconnect/reconnect 后恢复或明确报告 missing monitor；
6. DPI/topology change 后 expected/live geometry 一致；
7. Widget 仍位于 wallpaper 上、desktop icons 下。

自动 health 检查可以确认数值状态，但不能完全替代真实桌面目视验收。
