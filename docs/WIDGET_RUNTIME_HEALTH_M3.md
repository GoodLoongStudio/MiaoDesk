# TuringDesk M3 Widget runtime health contract

Status: active implementation contract
Date: 2026-08-25

M3 turns Widget runtime health from a single compatibility string into a caller-facing per-surface contract shared by UI, Pi and the future editor through `DesktopControlService::GetSnapshot()`.

## Ownership

```text
Web Widget runtime
    -> runtime diagnostics / HWNDs
    -> WidgetService
    -> WidgetRuntimeHealth
    -> DesktopSnapshot
    -> UI / Pi / future Editor
```

UI and Pi must not read `wallpaper.ini`, enumerate runtime HWNDs, or inspect WebView2 processes themselves. The Widget domain owns that translation.

## Current structured surface state

`WidgetSurfaceHealth` currently exposes, per enabled Web Widget:

- configured Widget id;
- isolated process id and running state;
- child HWND value and readiness;
- expected desktop-surface parent relationship;
- `WS_CHILD` style validity;
- current visibility;
- explicit WebView2 Environment / Controller / Navigation fields with matching `*Reported` flags;
- explicit z-order value with `zOrderReported`;
- compatibility rendering-health result;
- actionable per-surface detail text.

The first M3 slice intentionally reports WebView2 lifecycle and authoritative z-order as **unreported**, rather than inferring those stages from HWND existence. This prevents false healthy states while the child runtime telemetry channel is being added.

## Health semantics

An enabled Web Widget is OS-surface-ready only when its isolated process is running, its HWND exists, its parent matches the WallpaperHost desktop parent, it has `WS_CHILD`, and it is visible.

Aggregate `WidgetRuntimeHealth::runtimeHealthy` additionally requires the existing runtime compatibility diagnostic to be healthy and every enabled Web Widget to have an OS-ready surface. WebView2 lifecycle and z-order are not yet final M3 exit criteria until their `Reported` flags become true.

## Next M3 slices

1. Publish EnvironmentReady / ControllerReady / NavigationReady from the isolated WebView2 child without UI/Pi polling implementation details.
2. Publish authoritative DesktopShellHost z-order/role health for each Widget surface.
3. Make `renderingHealthy` require navigation + shell/z-order + visibility instead of compatibility inference.
4. Surface actionable failures in the Widget production UI.
5. Pass the real ARM64 Windows acceptance flow: create clock, keep it visible while Settings/Search open, recover after Explorer restart, and restore after display reconnect/change.

CI/self-tests validate contracts and regressions, but real Windows visible behavior remains the M3 exit gate.
