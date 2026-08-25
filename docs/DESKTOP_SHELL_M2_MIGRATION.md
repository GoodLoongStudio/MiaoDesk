# Desktop Shell M2 Migration

Status: active M2 implementation note
Date: 2026-08-25

## Goal

`DesktopShellHost` is the only implementation allowed to discover or mutate the Windows desktop attachment layer. Renderers, Web/Widget coordinators and UI code may manage their own surfaces, but they must not independently discover Progman/WorkerW, send `0x052C`, re-parent desktop surfaces or impose the desktop sibling z-order.

## Current production routing

### Native wallpaper host

The production target compiles `WallpaperEngineProduction.cpp`, not `WallpaperEngine.cpp` directly. While the legacy source cleanup is still in progress, the production bridge intercepts its shell APIs:

```text
legacy WallpaperEngine source intent
        ↓
WallpaperEngineProduction bridge
        ↓
DesktopShellHost
        ↓
Progman / WorkerW / parent / z-order
```

Intercepted operations:

- Progman lookup;
- WorkerW / `SHELLDLL_DefView` lookup;
- `0x052C` request;
- WallpaperHost re-parent;
- WallpaperHost / WorkerW desktop z-order intent.

The bridge does not create a second discovery implementation: it asks the shared `DesktopShellHost` for the current shell snapshot and delegates parent/z-order mutation through `AttachSurface` / `RepairSurfaceStack`.

The remaining M2 cleanup is to delete `DiscoverDesktopLayer`, `SpawnWallpaperLayer`, `DesktopLayer`, `TrySetParent`, `EnsureWorkerBottom` and the old mount-mode translation from the legacy source itself. Until that deletion lands, the ownership guard requires production interception to remain present.

### Web wallpaper and Widget runtime

`WallpaperWebRuntimeCoordinator` no longer enumerates Web sibling windows or calls `SetWindowPos` to maintain its own surface stack. It now owns only runtime/process intent and delegates shell state to one `DesktopShellHost` instance:

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

This also means Explorer generation changes and stale wallpaper-host parent recovery are handled through the same contract used by the native desktop host.

## Ownership guard

`scripts/verify-desktop-shell-ownership.ps1` now rejects:

- Progman / WorkerW / `SHELLDLL_DefView` discovery outside `DesktopShellHost`;
- `0x052C` outside `DesktopShellHost`;
- direct desktop `SetParent` in renderer/coordinator/Widget code;
- reintroduction of coordinator sibling enumeration / local z-order repair;
- removal of the production engine shell interception before legacy source cleanup is complete.

## Completion boundary

M2 is **not complete yet**. Production behavior is now substantially centralized, but the legacy engine source still contains obsolete shell-discovery helpers. M2 closes only after those helpers are removed, the exact `main` SHA passes ARM64 CI, and the shared shell contract remains the only desktop-attachment implementation.

Real Windows visible behavior remains an acceptance requirement; CI alone does not prove Widget or wallpaper layering correctness.
