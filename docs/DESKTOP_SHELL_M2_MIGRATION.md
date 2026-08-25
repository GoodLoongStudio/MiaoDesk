# Desktop Shell M2 Migration

Status: active M2 implementation note
Date: 2026-08-25

## Goal

`DesktopShellHost` is the only implementation allowed to discover or mutate the Windows desktop attachment layer. Renderers, Web/Widget coordinators and UI code may manage their own surfaces, but they must not independently discover Progman/WorkerW, send `0x052C`, re-parent desktop surfaces, map desktop geometry into shell-parent coordinates, or impose the desktop sibling z-order.

## Current production routing

### Native wallpaper host

The production target compiles `WallpaperEngineProduction.cpp`, not `WallpaperEngine.cpp` directly. While the legacy source cleanup is still in progress, the production bridge intercepts its shell APIs:

```text
legacy WallpaperEngine source intent
        ↓
WallpaperEngineProduction bridge
        ↓
DesktopShellHost::EnsureSurface
        ↓
Progman / WorkerW / parent / geometry / visibility / z-order
```

Intercepted operations:

- Progman lookup;
- WorkerW / `SHELLDLL_DefView` lookup;
- `0x052C` request;
- WallpaperHost re-parent;
- WallpaperHost geometry and visibility changes;
- WallpaperHost / WorkerW desktop z-order intent.

`EnsureSurface()` is the idempotent production attachment entry point. The caller supplies only a surface HWND, role, desired desktop-space bounds and visibility. `DesktopShellHost` then owns Explorer-generation validation, shell parent choice, child/layered styles, desktop-to-parent mapping, re-parenting and final wallpaper/Widget/icon stack repair.

The production bridge no longer performs its own WallpaperHost `SetWindowPos` after attachment. Both the intercepted `SetParent` path and later geometry/visibility updates converge on `EnsureSurface()`.

The remaining M2 cleanup is to delete `DiscoverDesktopLayer`, `SpawnWallpaperLayer`, `DesktopLayer`, `TrySetParent`, `EnsureWorkerBottom` and the old mount-mode translation from the legacy source itself. Until that deletion lands, the ownership guard requires production interception to remain present.

### Web wallpaper and Widget runtime

`WallpaperWebRuntimeCoordinator` no longer enumerates Web sibling windows or maintains its own desktop sibling z-order. It owns runtime/process intent and delegates shell state to `DesktopShellHost`:

```text
WallpaperWebRuntimeCoordinator
        ↓
EnsureCurrent
InspectSurface
RecoverSurface
SurfaceParent
RepairSurfaceStack
        ↓
DesktopShellHost
```

`RecoverSurface()` now reuses `EnsureSurface()` when a surface has a stale parent/style/geometry, so Explorer restart recovery and normal attachment share the same mutation path.

This establishes one attachment family for native wallpaper, Web wallpaper and Widget surfaces: `AttachSurface / EnsureSurface / RecoverSurface`, all implemented by `DesktopShellHost`.

## Ownership guard

`scripts/verify-desktop-shell-ownership.ps1` now rejects:

- Progman / WorkerW / `SHELLDLL_DefView` discovery outside `DesktopShellHost`;
- `0x052C` outside `DesktopShellHost`;
- direct desktop `SetParent` in renderer/coordinator/Widget code;
- reintroduction of coordinator sibling enumeration / local z-order repair;
- production WallpaperHost geometry mutation that bypasses `EnsureSurface()`;
- removal of the production engine shell interception before legacy source cleanup is complete.

The guard also requires the mixed-monitor negative-coordinate geometry self-test and the centralized Explorer-generation recovery contract to remain present.

## Completion boundary

M2 is **not complete yet**. Production behavior now has one effective shell attachment implementation, but `WallpaperEngine.cpp` still physically contains obsolete discovery/attachment helpers. M2 closes only after those helpers are removed from the legacy source, the exact `main` SHA passes ARM64 CI, and the shared shell contract remains the only desktop-attachment implementation.

Real Windows visible behavior remains an acceptance requirement; CI alone does not prove Widget or wallpaper layering correctness.
