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

The coordinator still contains a legacy independent-layout helper that expresses its host resize as a parent-client `SetWindowPos`. Production no longer compiles that source directly. `WallpaperWebRuntimeCoordinatorProduction.cpp` intercepts that remaining call, converts the requested parent-client rectangle back into desktop-space coordinates and routes it through `DesktopShellHost::EnsureSurface`.

```text
Independent layout host resize
        ↓
WallpaperWebRuntimeCoordinatorProduction
        ↓
parent-client rect → desktop-space rect
        ↓
DesktopShellHost::EnsureSurface(Wallpaper)
```

The bridge now preserves visibility semantics exactly: `SWP_SHOWWINDOW` forces visible, `SWP_HIDEWINDOW` forces hidden, and a geometry-only `SetWindowPos` preserves the existing visibility instead of accidentally showing a hidden wallpaper. Invalid zero/negative size requests are normalized before the desktop-space transaction. Failed `EnsureSurface` calls publish a stable Win32 failure code rather than silently pretending the geometry mutation succeeded.

This closes the production geometry-ownership exception without changing the coordinator's runtime/process behavior. The bridge is transitional and must be deleted when `EnsureIndependentHostBounds` is physically removed from the legacy coordinator source.

### Explorer generation and stale-parent recovery

`CurrentGenerationValid()` validates not only the cached Progman/Explorer PID but also mode-specific parent relationships:

- Raised Desktop requires both `SHELLDLL_DefView` and WorkerW to remain direct Progman children;
- Legacy WorkerW validates the WorkerW HWND and, when available, the DefView/legacy-parent relationship;
- Progman fallback rejects a DefView whose parent no longer matches either the cached Progman or cached legacy DefView parent.

`RecoverSurface()` records whether the cached Explorer generation was valid before refresh. If the generation changed, the surface always passes through `EnsureSurface()` even when recycled HWND values appear plausible. This prevents stale Explorer ownership from surviving a restart by coincidence.

`RecoverSurface()` therefore shares the same parent/style/geometry/z-order mutation path as first attachment.

This establishes one production attachment family for native wallpaper, Web wallpaper and Widget surfaces: `AttachSurface / EnsureSurface / RecoverSurface`, all implemented by `DesktopShellHost`.

## Ownership guard

`scripts/verify-desktop-shell-ownership.ps1` rejects:

- Progman / WorkerW / `SHELLDLL_DefView` discovery outside `DesktopShellHost`;
- `0x052C` outside `DesktopShellHost`;
- direct desktop `SetParent` in renderer/coordinator/Widget code;
- reintroduction of coordinator sibling enumeration / local z-order repair;
- direct production compilation of legacy `WallpaperWebRuntimeCoordinator.cpp`;
- removal/bypass of the coordinator `EnsureSurface` production bridge while the legacy resize helper remains;
- coordinator visibility logic that treats every non-hide resize as an implicit show;
- production WallpaperHost geometry mutation that bypasses `EnsureSurface()`;
- removal of the production engine shell interception before legacy source cleanup is complete.

The guard also requires the mixed-monitor negative-coordinate geometry self-test and the centralized Explorer-generation recovery contract to remain present.

## Completion boundary

M2 is **not complete yet**. Production behavior now has one effective shell attachment/geometry implementation, but `WallpaperEngine.cpp` still physically contains obsolete discovery/attachment helpers and `WallpaperWebRuntimeCoordinator.cpp` still contains the transitional independent-layout resize helper. M2 closes only after those helpers are removed from the legacy sources, the exact `main` SHA passes ARM64 CI, and the shared shell contract remains the only desktop-attachment implementation.

Real Windows visible behavior remains an acceptance requirement; CI alone does not prove Widget or wallpaper layering correctness.
