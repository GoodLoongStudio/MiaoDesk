# Desktop Shell M2 Migration

Status: implementation complete; exact-head CI and real-Windows acceptance pending
Date: 2026-08-25

## Goal

`DesktopShellHost` is the only implementation allowed to discover or mutate the Windows desktop attachment layer. Renderers, Web/Widget coordinators and UI code may manage their own surfaces, but they must not independently discover Progman/WorkerW, send `0x052C`, re-parent desktop surfaces, map desktop geometry into shell-parent coordinates, or impose the desktop sibling z-order.

## Current production routing

### Native wallpaper host

`WallpaperEngine.cpp` now includes and owns a `DesktopShellHost` client directly. Its `AttachToDesktop()` method no longer discovers Progman/WorkerW, sends `0x052C`, calls `SetParent`, maps desktop geometry into shell-parent coordinates, or repairs WorkerW ordering itself.

```text
WallpaperEngine runtime intent
        ↓
DesktopShellHost::EnsureCurrent
DesktopShellHost::EnsureSurface
DesktopShellHost::InspectSurface
DesktopShellHost::CurrentGenerationValid
        ↓
Progman / WorkerW / parent / geometry / visibility / z-order
```

`DesktopLayer`, `DiscoverDesktopLayer`, `SpawnWallpaperLayer`, `TrySetParent`, `EnsureWorkerBottom`, the old MountMode translation and parent-client geometry path have been physically removed from `WallpaperEngine.cpp`.

The production compatibility wrapper no longer intercepts FindWindow/WorkerW/`0x052C`/SetParent/SetWindowPos. It remains only for the earlier M1 persistence adapters (`PerformanceUiAdapter` and `AutomationUiAdapter`) until those compatibility macros can be removed independently of M2.

`EnsureSurface()` is the idempotent production attachment entry point. The caller supplies only a surface HWND, role, desired desktop-space bounds and visibility. `DesktopShellHost` owns Explorer-generation validation, shell parent choice, child/layered styles, desktop-to-parent mapping, re-parenting and final wallpaper/Widget/icon stack repair.

### Web wallpaper and Widget runtime

The Web/Widget coordinator has no production shell bridge. `WallpaperWebRuntimeCoordinator.cpp` is compiled directly and owns runtime/process intent only:

```text
WallpaperWebRuntimeCoordinator
        ↓
EnsureCurrent
InspectSurface
RecoverSurface
EnsureSurface
SurfaceParent
RepairSurfaceStack
        ↓
DesktopShellHost
```

Independent-layout host geometry is expressed directly as desktop-space `HostDesktopBounds(...)` and passed to `DesktopShellHost::EnsureSurface`. The coordinator does not call `DesktopRectToParentClient`, `SetParent`, `SetWindowPos`, or any Progman/WorkerW discovery API.

The old `EnsureIndependentHostBounds` helper and `WallpaperWebRuntimeCoordinatorProduction.cpp` interception bridge have been physically removed. Geometry-only refresh preserves the host's existing visibility by passing `IsWindowVisible(host)` to `EnsureSurface`; it does not implicitly show a hidden wallpaper.

### Explorer generation and stale-parent recovery

`CurrentGenerationValid()` validates not only the cached Progman/Explorer PID but also mode-specific parent relationships:

- Raised Desktop requires both `SHELLDLL_DefView` and WorkerW to remain direct Progman children;
- Legacy WorkerW validates the WorkerW HWND and, when available, the DefView/legacy-parent relationship;
- Progman fallback rejects a DefView whose parent no longer matches either the cached Progman or cached legacy DefView parent.

`RecoverSurface()` records whether the cached Explorer generation was valid before refresh. If the generation changed, the surface always passes through `EnsureSurface()` even when recycled HWND values appear plausible. This prevents stale Explorer ownership from surviving a restart by coincidence.

This establishes one production attachment family for native wallpaper, Web wallpaper and Widget surfaces: `AttachSurface / EnsureSurface / RecoverSurface`, all implemented by `DesktopShellHost`.

## Ownership guard

`scripts/verify-desktop-shell-ownership.ps1` now rejects:

- Progman / WorkerW / `SHELLDLL_DefView` discovery outside `DesktopShellHost` production ownership;
- `0x052C` outside `DesktopShellHost`;
- direct desktop `SetParent` in WallpaperEngine/renderer/coordinator/Widget code;
- reintroduction of `DesktopLayer`, `DiscoverDesktopLayer`, `SpawnWallpaperLayer`, `TrySetParent` or `EnsureWorkerBottom` in the legacy engine;
- coordinator sibling enumeration/local z-order repair;
- coordinator `EnsureIndependentHostBounds`, `DesktopRectToParentClient` or `SetWindowPos` geometry ownership;
- reintroduction of `WallpaperWebRuntimeCoordinatorProduction.cpp`;
- reintroduction of production FindWindow/WorkerW/`0x052C`/SetParent/SetWindowPos interception wrappers.

The guard also requires mixed-monitor negative-coordinate geometry self-tests and the centralized Explorer-generation recovery contract.

## Completion boundary

The **M2 implementation work is complete in source**. M2 is still **not accepted as complete** until the exact current `main` SHA passes ARM64 CI and the real-Windows layering flow verifies wallpaper + Widget placement below desktop icons, Explorer restart recovery, and mixed-monitor behavior.

CI proves build and architecture contracts; it does not prove that Widget/wallpaper/icon layering is visually correct on a real Windows desktop.
