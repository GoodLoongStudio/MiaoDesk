# TuringDesk C++ Wallpaper Implementation Reference

Status: authoritative implementation reference for the Windows wallpaper runtime.
Date: 2026-08-24
Product baseline: `docs/TURINGDESK-PRODUCT-BASELINE.md`
Capability roadmap: `docs/WALLPAPER_ENGINE_PARITY.md`
Desktop composition architecture: `docs/DESKTOP_COMPOSITION_ARCHITECTURE.md`

## 1. Purpose

TuringDesk targets Wallpaper Engine-class product depth, but **Wallpaper Engine is not the implementation reference**.

For Windows desktop integration, wallpaper runtime behavior, multi-monitor handling, playback lifecycle, Web wallpaper hosting, Explorer recovery and screensaver behavior, the primary mature open-source reference is:

- `rocksdanister/lively`

Lively is used as a behavioral and architectural reference only. TuringDesk reimplements the required behavior in native C++23.

## 2. License boundary

Lively is GPL-3.0. TuringDesk is MIT.

Therefore:

- do not copy Lively source code into TuringDesk;
- do not mechanically translate Lively C# source line-by-line into C++;
- do not reuse GPL implementation files, comments or project-specific code structure as TuringDesk source;
- it is acceptable to study public behavior, Windows API sequences, edge cases and compatibility strategies, then independently implement the same class of behavior in TuringDesk;
- when a Lively implementation detail is studied, record the behavior/API contract in TuringDesk documentation first, then implement it independently.

The goal is clean-room-style reimplementation of the Windows behavior, not source-code porting.

## 3. Product benchmark vs implementation reference

The distinction is mandatory:

```text
Product capability benchmark
    Wallpaper Engine-class feature depth
              ↓
TuringDesk product requirements
              ↓
Windows implementation reference
    Lively + Microsoft Windows APIs/docs
              ↓
Independent C++23 implementation
              ↓
TuringDesk runtime
```

Rules:

1. Product designers may use Wallpaper Engine-class workflows to define expected functionality.
2. Runtime engineers must not search Wallpaper Engine internals as the engineering source of truth.
3. Windows wallpaper implementation work starts from this document, Lively behavior, and Microsoft API behavior.
4. TuringDesk-specific AI, Widget and Desktop Control architecture remains authoritative where Lively has no equivalent.

## 4. C++ subsystem target

The wallpaper runtime should converge toward explicit native modules instead of accumulating desktop-shell behavior inside one large window class.

```text
TuringDeskWallpaper
├─ DesktopShellHost
│  ├─ Progman / WorkerW discovery
│  ├─ raised-desktop detection
│  ├─ desktop surface attachment
│  ├─ z-order repair
│  └─ Explorer restart recovery
├─ DisplayTopology
│  ├─ stable monitor identity
│  ├─ bounds / work area / DPI
│  └─ Span / Clone / Primary / Independent
├─ WallpaperSurfaceManager
│  ├─ ImageSurface
│  ├─ VideoSurface
│  ├─ WebSurface
│  └─ SceneSurface
├─ WidgetSurfaceManager
│  ├─ WebWidgetSurface
│  └─ future native widgets
├─ PlaybackCoordinator
│  ├─ pause / resume / stop / throttle
│  ├─ fullscreen / maximized / per-app policy
│  └─ battery / lock / RDP / idle policy
├─ DesktopLifecycleMonitor
│  ├─ Explorer / WorkerW rebuild
│  ├─ display topology change
│  ├─ session / power events
│  └─ device-loss recovery
└─ DesktopDiagnostics
   ├─ configured state
   ├─ process state
   ├─ HWND state
   ├─ WebView state
   ├─ z-order state
   └─ visible/rendering health
```

AI and Settings never manipulate these modules directly. They go through the Desktop Control API.

## 5. Desktop shell mounting contract

### 5.1 Detect the Windows desktop model

The implementation must distinguish at least:

```text
Legacy desktop
  Progman / top-level WorkerW layout

Windows 11 raised desktop
  Progman has WS_EX_NOREDIRECTIONBITMAP
  SHELLDLL_DefView is a layered child
  wallpaper WorkerW is a child below the icon layer
```

Do not assume a single WorkerW layout works on all Windows 10/11 revisions.

### 5.2 Request the wallpaper layer

Use the established Progman `0x052C` sequence only through a dedicated `DesktopShellHost` abstraction. The rest of the renderer must not know the undocumented shell message directly.

The shell host must then rediscover the actual desktop hierarchy; never retain stale handles across Explorer rebuilds.

### 5.3 Raised desktop behavior

For raised-desktop systems, the C++ implementation should follow the compatible behavior demonstrated by mature projects such as Lively:

- use `Progman` as the desktop parent for the custom render surface;
- make the render surface a child window;
- make the custom render surface layered when the raised desktop requires it;
- set full opacity for compositor participation rather than using per-pixel transparency for the normal wallpaper surface;
- place the wallpaper surface below `SHELLDLL_DefView` and above the wallpaper WorkerW;
- verify and repair WorkerW ordering after attachment and after shell changes.

A successfully created HWND is not sufficient proof of success. The runtime must verify parent, styles, z-order and rendering readiness.

### 5.4 Legacy WorkerW behavior

When the system is not using raised desktop:

- locate the WorkerW that is behind the desktop icon view;
- attach the wallpaper surface to that WorkerW;
- compute coordinates relative to the selected desktop parent;
- reattach after Explorer or display hierarchy changes.

Fallback to Progman is allowed only when diagnostics record why the preferred path failed.

## 6. Web wallpaper and Web widget hosting

Web wallpaper and Web widget surfaces must use isolated WebView2 hosts, but they are still desktop surfaces and must obey the same shell attachment contract.

The target lifecycle is:

```text
Create native host HWND
  ↓
Attach HWND through DesktopShellHost
  ↓
Verify style / parent / z-order
  ↓
Create WebView2 controller
  ↓
Navigate validated source
  ↓
Wait for ready state
  ↓
Report visible/rendering health
```

Important requirements:

- do not treat process creation as proof that a Web surface is visible;
- do not treat an enabled Widget manifest record as proof that a Widget is visible;
- WebView2 user-data directories remain isolated by wallpaper/widget identity where needed;
- navigation, permissions, popup behavior and external protocols remain restricted;
- recovery must distinguish WebView process crash, controller creation failure, desktop attachment failure and hidden/z-order failure;
- Widget and Web wallpaper surfaces must not accidentally pause simply because a TuringDesk-owned Settings/Search window is open.

## 7. Wallpaper and Widget z-order

TuringDesk has an additional requirement beyond a conventional wallpaper engine: persistent Widgets.

Logical composition:

```text
Desktop icon layer
------------------
Widget Layer
------------------
Wallpaper Layer
------------------
Windows wallpaper WorkerW/background
```

The exact HWND ordering depends on the active Windows desktop model, but these invariants are mandatory:

- Wallpaper is below desktop icons.
- Default Widget mode is also below desktop icons so icons remain usable.
- Widget must be above the TuringDesk wallpaper surface.
- Interactive Widget mode, if added later, must be explicit and must not globally break normal desktop input.
- Z-order repair is centralized; individual Widget/Web processes do not independently guess shell ordering.

## 8. Multi-monitor behavior

Use Lively as a mature behavior reference for the normal arrangements, while retaining TuringDesk's stable monitor identity model.

Required arrangements:

```text
Span
Clone / Duplicate
Primary only
Independent per monitor
```

Requirements:

- negative virtual-screen coordinates;
- mixed DPI;
- monitor reorder;
- disconnect / reconnect;
- stable monitor IDs for persisted assignments;
- per-monitor wallpaper and Widget geometry;
- WebView/video instances must be recreated or resized reliably when topology changes.

## 9. Playback and performance lifecycle

The wallpaper system must centralize runtime policy rather than letting each renderer invent pause logic.

Inputs include:

- foreground app;
- running app;
- fullscreen;
- maximized;
- playing audio when implemented;
- battery saver;
- lock/session state;
- Remote Desktop;
- idle;
- user policy.

Actions remain:

```text
Normal
Throttle
Pause
Stop
```

Renderers translate the selected policy to the most appropriate native operation. For example, Stop may destroy expensive Web/video resources while preserving the persisted desktop state for later restoration.

## 10. Explorer and lifecycle recovery

The runtime must explicitly handle:

- Explorer restart;
- WorkerW destruction/recreation;
- `TaskbarCreated`/shell recreation;
- display settings changes;
- sleep/resume;
- lock/unlock;
- Remote Desktop transition;
- WebView process failure;
- GPU device loss.

On shell rebuild:

```text
invalidate cached shell HWNDs
  ↓
rediscover desktop hierarchy
  ↓
reattach active wallpaper surfaces
  ↓
reattach active Widget surfaces
  ↓
restore z-order
  ↓
verify visible health
```

## 11. Screensaver direction

Screensaver support is part of basic wallpaper-product parity, not an editor-only feature.

The implementation should reuse the same wallpaper/profile runtime where practical instead of creating a second renderer. Screensaver mode may use a different host/lifecycle, but asset parsing, playback, properties and profiles should remain shared.

## 12. Diagnostics contract

Every desktop surface should eventually expose a structured state similar to:

```text
configured=true
processStarted=true
hwndCreated=true
parentValid=true
layeredRequired=true
layeredApplied=true
zOrderValid=true
webViewReady=true
navigationReady=true
visible=true
renderingHealthy=true
lastError=""
```

This is particularly important for Widgets. The UI must never show only `已启用` when the runtime cannot prove that the surface is actually present.

## 13. Implementation migration order

The current code should be migrated in this order:

1. Extract `DesktopShellHost` from `WallpaperEngine.cpp`.
2. Rebuild Web wallpaper / Widget surface attachment on the shared shell host.
3. Add raised-desktop style and z-order validation based on known-good Windows behavior.
4. Add visible-surface diagnostics and expose them in the Widgets/Wallpaper UI.
5. Move Explorer/display lifecycle recovery into `DesktopLifecycleMonitor`.
6. Consolidate multi-monitor surface placement.
7. Consolidate playback/performance policy.
8. Add screensaver mode.
9. Continue higher-level Wallpaper Engine-class features: Properties, Editor, Particle, Shader, Audio Reactive and 3D.

## 14. Acceptance standard

A shell/runtime item is complete only when it passes a real interactive Windows user flow.

Minimum interactive checks:

- native Scene is visible behind icons;
- Image is visible behind icons;
- Video is visible behind icons;
- local Web wallpaper is visible behind icons;
- Web Widget is visible in its configured monitor region;
- desktop icons remain usable;
- opening TuringDesk Settings does not hide/pause the Widget incorrectly;
- taskbar remains usable;
- monitor disconnect/reconnect restores surfaces;
- Explorer restart restores surfaces;
- fullscreen app policy pauses/throttles/stops and then restores correctly.

CI and self-tests validate code paths and packaging, but cannot substitute for these interactive shell checks.

## 15. Source-of-truth rule

For future wallpaper implementation work:

```text
Product behavior target       -> TURINGDESK-PRODUCT-BASELINE.md
Capability backlog            -> WALLPAPER_ENGINE_PARITY.md
Windows runtime implementation -> LIVELY_CPP_WALLPAPER_IMPLEMENTATION.md
Desktop/Widget layering       -> DESKTOP_COMPOSITION_ARCHITECTURE.md
AI control contract           -> L3-PI-RUNTIME-CONTRACT.md
```

Do not create another parallel wallpaper architecture document unless this source-of-truth map is updated first.
