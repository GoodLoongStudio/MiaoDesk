# Desktop Shell Recovery Contract

Status: M2 implementation contract
Date: 2026-08-25

## Ownership

`DesktopShellHost` is the only component allowed to discover or repair Windows desktop shell attachment state. Renderers, Web wallpaper coordinators, Widgets and future editors must not rediscover `Progman`, `WorkerW` or `SHELLDLL_DefView`, send `0x052C`, or call `SetParent` to claim desktop ownership.

## Generation model

A `DesktopShellSnapshot` identifies one Explorer desktop generation using:

- `Progman` HWND;
- current Explorer process id;
- shell mode;
- `SHELLDLL_DefView` / `WorkerW` handles where required;
- monotonically increasing `generation`.

`CurrentGenerationValid()` is a read-only check. It validates that the cached `Progman` is still the current shell window, the Explorer PID still matches and the mode-specific parent chain remains valid.

## Recovery entry point

A surface that detects stale attachment calls:

```text
DesktopShellHost::RecoverSurface(surface, role)
```

The caller supplies only its own HWND and semantic role (`Wallpaper` or `Widget`). `DesktopShellHost` then:

1. captures the surface screen-space geometry and visibility;
2. refreshes the Explorer/shell generation through `EnsureCurrent()`;
3. checks current parent/style/layer/geometry health;
4. reattaches the surface through `AttachSurface()` when its parent is stale;
5. repairs the shared wallpaper/Widget z-order stack.

This prevents each renderer from inventing its own Explorer restart recovery path.

## Multi-monitor geometry

The shared monitor-layout self-test covers a virtual desktop with both negative X and negative Y coordinates:

```text
virtualBounds = {-1920, -240, 3840, 2160}
```

It validates Span, PrimaryOnly, Clone and Independent host regions. The M2 build guard now requires this negative-coordinate test to remain present.

## Remaining M2 migration

The following tracked exceptions still prevent M2 from completing:

- `WallpaperEngine.cpp` still contains legacy `DiscoverDesktopLayer()` / `SpawnWallpaperLayer()` and direct `Progman` discovery.
- `WallpaperWebRuntimeCoordinator.cpp` still contains its legacy `MaintainDesktopSurfaceZOrder()` implementation.

These exceptions must be removed rather than normalized into permanent architecture.
