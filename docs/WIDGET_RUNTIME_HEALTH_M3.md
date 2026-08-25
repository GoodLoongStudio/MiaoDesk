# TuringDesk M3 Widget runtime health contract

Status: active implementation contract
Date: 2026-08-25

M3 turns Widget runtime health from a single compatibility string into a caller-facing per-surface contract shared by UI, Pi and the future editor through `DesktopControlService::GetSnapshot()`.

## Ownership

```text
WebDesktopSurfaceChild
    -> HWND lifecycle properties
DesktopShell read-only z-order telemetry
    -> WidgetService
    -> WidgetRuntimeHealth
    -> DesktopSnapshot
    -> UI / Pi / future Editor
```

UI and Pi must not read `wallpaper.ini`, enumerate runtime HWNDs, inspect WebView2 processes, or interpret sibling ordering themselves. The Widget domain owns runtime-health translation, while desktop/shell owns shared z-order semantics and all shell mutation remains in `DesktopShellHost`.

## Current structured surface state

`WidgetSurfaceHealth` exposes, per enabled Web Widget:

- configured Widget id;
- isolated process id and running state;
- child HWND value and readiness;
- expected desktop-surface parent relationship;
- `WS_CHILD` style validity;
- current visibility;
- WebView2 EnvironmentReady / ControllerReady / NavigationReady with matching `*Reported` flags;
- z-order reported/valid state from shared desktop/shell telemetry;
- combined rendering-health result;
- actionable per-surface detail text.

The preferred `WebDesktopSurfaceChild` already publishes process-safe HWND properties when Environment creation, Controller creation and successful NavigationCompleted finish. `WidgetService` consumes those properties. The child role property exists before asynchronous WebView2 initialization, so a missing readiness property is a real not-ready stage rather than an inferred failure. The legacy Web child has no role property and therefore remains explicitly unreported instead of being falsely marked ready.

Z-order inspection is implemented in `desktop/shell/DesktopSurfaceTelemetry.cpp`. It is read-only: it checks sibling order but contains no `SetParent`, `SetWindowPos`, WorkerW discovery or repair behavior. `DesktopShellHost` remains the sole desktop-attachment/z-order mutation owner. The shared telemetry verifies that desktop icon `SHELLDLL_DefView`, when it shares the parent, stays above TuringDesk surfaces and that Widget surfaces stay above TuringDesk wallpaper surfaces.

## Health semantics

An enabled Web Widget is OS-surface-ready only when its isolated process is running, its HWND exists, its parent matches the WallpaperHost desktop parent, it has `WS_CHILD`, and it is visible.

For the preferred child path, lifecycle readiness additionally requires EnvironmentReady + ControllerReady + NavigationReady. Rendering health also requires reported/valid shared z-order telemetry and the existing compatibility runtime diagnostic. Aggregate `WidgetRuntimeHealth::runtimeHealthy` requires a one-to-one structured surface for every enabled Web Widget and every surface to be rendering healthy.

A legacy child can still be observed without inventing lifecycle readiness: its lifecycle fields remain unreported, so clients can distinguish a compatibility fallback from the preferred fully-telemetried path.

## Remaining M3 slices

1. Expose the per-surface structured fields clearly through Pi `wallpaper_state_get` and the production Widget UI rather than showing only the aggregate compatibility summary.
2. Add actionable production UI status/errors for process/HWND/Environment/Controller/Navigation/parent/style/z-order/visibility failures.
3. Confirm the read-only z-order interpretation against real ARM64 Windows in Raised Desktop, legacy WorkerW and Progman fallback modes.
4. Pass the real ARM64 Windows acceptance flow: create clock, keep it visible while Settings/Search open, recover after Explorer restart, and restore after display reconnect/change.

CI/self-tests validate contracts and regressions, but real Windows visible behavior remains the M3 exit gate.
