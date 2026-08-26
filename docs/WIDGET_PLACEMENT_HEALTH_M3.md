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

## Independent acceptance verification

The sealed-evidence session verifier must not trust only the aggregate `runtimeHealthy` or per-surface `renderingHealthy` flag. For every enabled Widget section in every acceptance phase it independently requires the report to contain all of these successful facts:

```text
monitorValid=true
geometryValid=true
visible=true
zOrderValid=true
renderingHealthy=true
```

The number of successful values for each field must exactly match the number of enabled Widget sections. This keeps monitor placement and desktop layering as explicit evidence even if the implementation of the aggregate health flag evolves later.

## monitor reconnect acceptance

The M3 `monitor` phase is valid only when the display topology transition evidence exists and the post-transition Widget health is healthy. Placement consistency is part of `renderingHealthy`, and the independent verifier also checks target-monitor validity, geometry, visibility and z-order separately. The phase therefore cannot pass merely because a process or surface is alive.

This strengthens automated evidence but does not replace the real-Windows visual gate. A human must still confirm the Widget is visually correct, remains below desktop icons/above TuringDesk wallpaper, and returns to the intended monitor after disconnect/reconnect.
