# M3 Widget placement health contract

Status: active M3 runtime/acceptance contract
Date: 2026-08-26

M3 must prove more than process/HWND/WebView2 readiness. A Widget that survives a display topology change but returns on the wrong monitor or at the wrong geometry is not healthy.

## Ownership

Placement health is produced by `WidgetService` and consumed through `WidgetRuntimeHealth` / `WidgetSurfaceHealth` in `DesktopSnapshot`.

UI, Pi and acceptance tooling remain consumers. They must not enumerate or mutate desktop attachment HWNDs themselves. `DesktopShellHost` remains the only Windows desktop attachment and z-order mutation owner.

## Per-surface placement telemetry

For every enabled Web Widget, `WidgetSurfaceHealth` reports:

```text
monitorId
monitorReported
monitorValid
geometryReported
geometryValid
expectedLeft / expectedTop / expectedRight / expectedBottom
actualLeft / actualTop / actualRight / actualBottom
```

The expected desktop-space rectangle is derived from the persisted normalized Widget rectangle and the currently resolved target monitor. An empty persisted monitor id resolves to the current primary monitor. A non-empty id must resolve through the shared monitor-layout stable-id contract.

The actual rectangle is read from the live Widget surface. `geometryValid` allows only a small pixel tolerance so normal Win32 rounding does not create false failures.

## Health semantics

`WidgetSurfaceHealth::SurfaceReady()` now requires all of the following before rendering can be considered healthy:

- configured Web Widget;
- isolated process running;
- HWND ready;
- expected parent valid;
- `WS_CHILD` valid;
- visible;
- monitor topology reported;
- target monitor resolved;
- live geometry reported;
- live geometry matches the expected desktop-space rectangle.

Existing WebView2 lifecycle and z-order checks are still required by `renderingHealthy`.

Stable placement issue codes include:

```text
monitor_topology_unavailable
monitor_missing
geometry_unreported
geometry_mismatch
```

These issue codes and recommended actions are generated inside the Widget domain so UI/Pi do not infer monitor or HWND remediation rules.

## Monitor reconnect acceptance

The M3 `monitor` phase is valid only when the display topology transition evidence exists and the post-transition Widget health is healthy. Because placement consistency is now part of `renderingHealthy`, the phase cannot pass merely because a surface is visible: it must also resolve the configured target monitor and recover to the expected geometry.

This strengthens automated evidence but does not replace the real-Windows visual gate. A human must still confirm the Widget is visually correct, remains below desktop icons/above TuringDesk wallpaper, and returns to the intended monitor after disconnect/reconnect.
