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

Intercepted operations include Progman/WorkerW/DefView lookup, `0x052C`, WallpaperHost re-parenting, geometry/visibility and desktop z-order intent.

`EnsureSurface()` is the idempotent production attachment entry point. The caller supplies only a surface HWND, role, desired desktop-space bounds and visibility. `DesktopShellHost` owns Explorer-generation validation, shell parent choice, child/layered styles, desktop-to-parent mapping, re-parenting and final wallpaper/Widget/icon stack repair.

The remaining native-engine M2 cleanup is to physically delete `DiscoverDesktopLayer`, `SpawnWallpaperLayer`, `DesktopLayer`, `TrySetParent`, `EnsureWorkerBottom`, the old mount-mode translation and the legacy `AttachToDesktop` shell implementation from `WallpaperEngine.cpp`. Until that deletion lands, the ownership guard requires production interception to remain present.

### Web wallpaper and Widget runtime

The Web/Widget coordinator no longer has a production bridge. `WallpaperWebRuntimeCoordinator.cpp` is compiled directly and owns runtime/process intent only:

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

Independent-layout host geometry is now expressed directly as desktop-space `HostDesktopBounds(...)` and passed to `DesktopShellHost::EnsureSurface`. The coordinator no longer calls `DesktopRectToParentClient`, `SetParent`, `SetWindowPos`, or any Progman/WorkerW discovery API.

The old `EnsureIndependentHostBounds` helper and `WallpaperWebRuntimeCoordinatorProduction.cpp` interception bridge have been physically removed. Geometry-only refresh preserves the host's existing visibility by passing `IsWindowVisible(host)` to `EnsureSurface`; it does not implicitly show a hidden wallpaper.

### Explorer generation and stale-parent recovery

`CurrentGenerationValid()` validates not only the cached Progman/Explorer PID but also mode-specific parent relationships:

- Raised Desktop requires both `SHELLDLL_DefView` and WorkerW to remain direct Progman children;
- Legacy WorkerW validates the WorkerW HWND and, when available, the DefView/legacy-parent relationship;
- Progman fallback rejects a DefView whose parent no longer matches either the cached Progman or cached legacy DefView parent.

`RecoverSurface()` records whether the cached Explorer generation was valid before refresh. If the generation changed, the surface always passes through `EnsureSurface()` even when recycled HWND values appear plausible. This prevents stale Explorer ownership from surviving a restart by coincidence.

This establishes one production attachment family for native wallpaper, Web wallpaper and Widget surfaces: `AttachSurface / EnsureSurface / RecoverSurface`, all implemented by `DesktopShellHost`.

## Ownership guard

`scripts/verify-desktop-shell-ownership.ps1` rejects:

- Progman / WorkerW / `SHELLDLL_DefView` discovery outside `DesktopShellHost` production ownership;
- `0x052C` outside `DesktopShellHost`;
- direct desktop `SetParent` in renderer/coordinator/Widget code;
- coordinator sibling enumeration/local z-order repair;
- coordinator `EnsureIndependentHostBounds`, `DesktopRectToParentClient` or `SetWindowPos` geometry ownership;
- reintroduction of `WallpaperWebRuntimeCoordinatorProduction.cpp`;
- production WallpaperHost geometry mutation that bypasses `EnsureSurface()`;
- removal of the production engine shell interception before the remaining legacy engine source cleanup is complete.

The guard also requires mixed-monitor negative-coordinate geometry self-tests and the centralized Explorer-generation recovery contract.

## Completion boundary

M2 is **not complete yet**. The coordinator exception is closed and its transitional bridge has been deleted. The remaining blocker is physical removal of obsolete desktop discovery/attachment code from `WallpaperEngine.cpp`, followed by exact-head ARM64 validation and real-Windows layering acceptance.

CI proves build and architecture contracts; it does not prove that Widget/wallpaper/icon layering is visually correct on a real Windows desktop.
