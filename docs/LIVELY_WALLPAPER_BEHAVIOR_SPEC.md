# Lively wallpaper behavior study for MiaoDesk

Status: clean-room behavior study used before native C++ refactoring.
Date: 2026-08-24
Reference repository: `rocksdanister/lively`
Reference branch: `core-separation`
Reference revision studied: `c1036feb664960722e34bf4309042c247d6a909d`

This document records behavior, Windows API expectations and failure/recovery contracts learned from Lively. It is intentionally not a source-code port. MiaoDesk remains an independent C++23/MIT implementation.

## 1. Source modules studied

Primary behavior references:

- `src/Lively/Lively/Core/WinDesktopCore.cs` — desktop shell discovery, WorkerW/Progman attachment, raised-desktop handling, per/span/duplicate wallpaper placement, WorkerW destruction recovery.
- `src/Lively/Lively/Core/Display/DisplayManager.cs` — monitor enumeration, bounds/work-area refresh, virtual screen and display change behavior.
- `src/Lively/Lively/Core/Suspend/Playback.cs` — fullscreen/foreground/system-state playback policy and per-display pause decisions.
- `src/Lively/Lively.Player.WebView2/Form1.cs` — WebView2 process lifecycle, navigation completion, process-failure reporting, per-wallpaper user-data folder, scaling and IPC readiness.
- `src/Lively/Lively/Services/ScreensaverService.cs` — idle activation, multi-monitor screensaver layout, input exit, blank fallback and wallpaper reuse.
- `README.md` — user-facing capability contract for video/Web/application wallpapers, screensavers, automation, API and pause rules.

Other modules are studied only as needed when a MiaoDesk subsystem is implemented.

## 2. Key architectural lesson

The shell hierarchy is infrastructure, not renderer-specific behavior.

Wrong model:

```text
Image renderer -> finds WorkerW
Video renderer -> finds WorkerW again
Web renderer   -> guesses a parent
Widget runtime -> guesses z-order separately
```

Target model:

```text
DesktopShellHost
  -> discovers one current desktop hierarchy
  -> exposes validated attachment targets
  -> attaches all MiaoDesk desktop surfaces
  -> repairs ordering after Explorer/WorkerW changes

WallpaperSurfaceManager / WidgetSurfaceManager
  -> render only
```

No renderer is allowed to know the `0x052C` Progman message directly.

## 3. Windows desktop model

### 3.1 Legacy WorkerW model

Expected structure is conceptually:

```text
Top-level window containing SHELLDLL_DefView
next WorkerW sibling -> wallpaper target
Progman
```

Behavior contract:

1. Ask Explorer to materialize the wallpaper layer.
2. Enumerate top-level windows.
3. Find the top-level window containing `SHELLDLL_DefView`.
4. Find the WorkerW sibling following that window.
5. Parent the wallpaper surface to that WorkerW.
6. Map virtual-screen desktop coordinates into the selected parent client coordinates.
7. Re-run discovery after Explorer or display topology changes.

Do not cache WorkerW indefinitely.

### 3.2 Windows 11 raised desktop

Raised desktop is detected when `Progman` carries `WS_EX_NOREDIRECTIONBITMAP`.

The shell model differs:

```text
Progman
├─ SHELLDLL_DefView   # layered icon/text shell surface
├─ custom MiaoDesk surfaces
└─ WorkerW            # Windows background layer
```

The compatible surface contract is:

- custom render HWND is a child of `Progman`;
- the custom surface participates in layered composition;
- full alpha (`255`) is used for a normal opaque desktop surface;
- custom wallpaper surfaces must be below `SHELLDLL_DefView`;
- custom surfaces must remain above the background WorkerW;
- WorkerW must remain at the back of the raised-desktop child stack;
- parent/style/z-order must be revalidated after attachment and shell changes.

This is the first implementation rule to apply to MiaoDesk Web/Widget surfaces, because an existing process/HWND is not proof that DWM is actually composing it.

## 4. Desktop layer lifecycle

### 4.1 Initial setup

At startup:

```text
Find Progman
 -> detect raised vs legacy desktop
 -> request wallpaper layer
 -> rediscover DefView / WorkerW
 -> publish current DesktopShellSnapshot
 -> attach surfaces
```

A snapshot must contain at least:

```text
progman
shellDefView
workerW
legacyDefViewParent
mode: raised | legacy | fallback
explorerPid/generation
```

### 4.2 WorkerW destruction

Lively explicitly observes WorkerW destruction. MiaoDesk must model the same failure:

```text
WorkerW destroyed
 -> invalidate current shell snapshot
 -> rediscover shell hierarchy
 -> raised desktop: reattach/reorder existing surfaces
 -> legacy desktop: rebuild surfaces when safe reparenting is uncertain
 -> publish diagnostics/change event
```

A periodic timer can remain as a secondary health check, but WorkerW/Explorer events should become first-class lifecycle signals.

### 4.3 Explorer restart / TaskbarCreated

Explorer restart invalidates shell HWNDs even if process state for renderers is still alive.

Contract:

- treat `TaskbarCreated` as a shell generation change;
- rediscover `Progman`, `DefView`, WorkerW and Explorer PID;
- reattach native wallpaper surfaces;
- recreate or reattach Web/Widget surfaces based on runtime capability;
- repair z-order;
- do not trust old HWND parent comparisons after Explorer generation changes.

## 5. Surface roles and z-order

MiaoDesk adds Widgets, so the desired logical composition is:

```text
SHELLDLL_DefView / desktop icons
--------------------------------
MiaoDesk Widget surfaces
--------------------------------
MiaoDesk Web wallpaper surfaces
MiaoDesk native/video/scene wallpaper surfaces
--------------------------------
Windows WorkerW/background
```

Rules:

1. Desktop icons always stay above default MiaoDesk surfaces.
2. Widgets stay above wallpaper surfaces but remain below icons in default click-through mode.
3. Web wallpaper and Web Widget hosts use the same shell attachment service as native surfaces.
4. Each surface has an explicit role; z-order repair must not infer role only from random child-window order.
5. A successful attach requires verification of parent, style, geometry, visible state and expected z-order relation.

Suggested C++ enum:

```text
DesktopSurfaceRole::Wallpaper
DesktopSurfaceRole::Widget
```

Interactive Widget mode is a separate later policy; it must not redefine the default desktop stack.

## 6. Multi-monitor behavior

Lively exposes per-monitor, span and duplicate arrangements. MiaoDesk additionally keeps Primary-only and stable DisplayConfig identities.

The shared behavioral contract is:

- enumerate monitors after `WM_DISPLAYCHANGE` rather than mutating stale geometry;
- maintain virtual-screen bounds including negative coordinates;
- maintain monitor work area separately from monitor bounds;
- attach/rerender surfaces after topology changes;
- per-monitor surfaces use monitor-local geometry derived from current monitor bounds;
- duplicate mode creates synchronized per-monitor instances where required;
- span mode treats the virtual desktop as one composition surface;
- topology changes may require Web/video surface recreation, not only resize.

MiaoDesk keeps its stronger stable monitor ID mapping for persisted assignments.

## 7. Playback / performance policy

Lively centralizes policy rather than asking each renderer whether it should pause. MiaoDesk keeps this principle with the richer action set `Normal / Throttle / Pause / Stop`.

Inputs:

```text
foreground window
visible windows by display
fullscreen / display coverage
maximized window
per-app rule
lock state
Remote Desktop
AC/battery state
battery saver
idle state
screensaver state
user policy
```

Important behavior:

- foreground-only evaluation is useful and cheap, but not sufficient for every policy;
- per-display pause is meaningful for independent wallpapers;
- span wallpaper is normally treated as one runtime and should pause only according to the span policy;
- duplicate mode usually behaves as one synchronized wallpaper for pause decisions;
- audio policy is separate from visual playback policy;
- MiaoDesk-owned UI must be excluded so Settings/Search does not pause its own desktop.

The renderer receives a policy result; it does not rediscover foreground/system state independently.

## 8. Web wallpaper / Web Widget process contract

For each Web surface:

```text
validated source
 -> create isolated process
 -> create native surface HWND with shell-compatible styles
 -> attach/verify desktop parent and z-order
 -> create WebView2 environment/controller
 -> navigate
 -> wait for navigation-complete success
 -> report readiness
```

Runtime state must distinguish:

```text
Configured
ProcessStarted
HwndCreated
DesktopAttached
WebViewEnvironmentReady
WebViewControllerReady
NavigationReady
Visible
Healthy
```

`Enabled=true` in the Widget Store only means Configured. It must never be displayed as equivalent to Visible/Healthy.

### 8.1 What Lively's WebView2 player makes explicit

Useful behavior to preserve independently in C++:

- use a dedicated user-data folder instead of sharing arbitrary browser state across all wallpapers;
- controller/runtime initialization is asynchronous and must have an explicit failure path;
- report an HWND only after the WebView control is actually attached to its native host;
- navigation completion is a separate readiness milestone from controller creation;
- `ProcessFailed` must be observed as a distinct renderer failure;
- popups/new windows and downloads are controlled rather than silently opening arbitrary UI;
- per-wallpaper scaling can require explicit WebView2 rasterization/monitor-DPI handling;
- pause state changes renderer behavior, and a renderer failure while intentionally suspended should not always be treated as an ordinary crash;
- optional data services such as audio visualization/system information are initialized only when the loaded project requests them.

MiaoDesk v1 remains intentionally stricter: no arbitrary popup/download behavior and no unscoped data service access. Future Widget data providers must go through explicit permissions.

### 8.2 MiaoDesk Web child state machine

Target native state machine:

```text
Starting
 -> NativeWindowReady
 -> DesktopAttached
 -> EnvironmentReady
 -> ControllerReady
 -> Navigating
 -> NavigationReady
 -> Running

any stage -> Failed(reason, HRESULT/Win32/process code)
Paused <-> Running
Stopping -> Stopped
```

The process supervisor must receive enough state to distinguish a dead process from a live-but-black/unattached WebView2 surface.

Recovery classes must be separated:

- child process exit;
- WebView2 environment/controller failure;
- WebView2 renderer-process failure;
- navigation/source failure;
- stale desktop parent;
- wrong z-order/hidden HWND;
- display topology/DPI mismatch.

## 9. Application/game wallpapers

Lively can host application/game windows as wallpaper. MiaoDesk does not need to copy that implementation immediately, but the SurfaceManager must not hard-code all future surfaces as Direct2D/WebView2.

Target abstraction should allow:

```text
Native render HWND
Media/video HWND
WebView2 HWND
External owned process HWND (future, permission-gated)
```

This keeps parity expansion possible without changing DesktopShellHost.

## 10. Screensaver behavior

Lively treats screensaver as a separate presentation/runtime mode that can reuse wallpaper content across monitors rather than simply moving the ordinary desktop HWND to topmost.

Useful behavior contract:

- idle time starts screensaver only when policy permits;
- user input terminates it;
- display topology changes stop/rebuild the screensaver session rather than leaving stale full-screen windows;
- per-monitor arrangement starts one presentation surface per assigned display;
- span uses the virtual screen as one presentation rectangle;
- duplicate starts a copy on each display;
- missing/failed wallpaper content falls back to blank coverage so the screensaver still protects the display;
- audio is normally emitted only from the primary/selected screensaver surface to avoid duplicated sound;
- the ordinary wallpaper playback coordinator knows a dedicated screensaver runtime is active and can pause the normal desktop runtime;
- lock-on-resume/grace-period behavior is separate policy, not a property of the wallpaper renderer itself.

MiaoDesk target:

```text
ScreensaverCoordinator
  -> chooses Wallpaper / Playlist / Profile snapshot
  -> creates dedicated full-screen surfaces
  -> maps per/span/duplicate layout
  -> listens for input/display/session exit
  -> tears down atomically
```

Windows Control Panel preview integration is a later compatibility item. Screensaver remains P1 parity after DesktopShellHost and the ordinary surface lifecycle are stable.

## 11. MiaoDesk refactor mapping

Existing responsibility -> target module:

```text
WallpaperEngine.cpp::DiscoverDesktopLayer
WallpaperEngine.cpp::SpawnWallpaperLayer
WallpaperEngine.cpp::PrepareChildWindow
WallpaperEngine.cpp::AttachToDesktop
WallpaperEngine.cpp::EnsureWorkerBottom
    -> DesktopShellHost

WallpaperWebRuntimeCoordinator ad-hoc parent/z-order logic
    -> DesktopShellHost + DesktopSurfaceStack

IndependentWallpaperHost / VideoWallpaperSet / WebWallpaperProcessSet
    -> WallpaperSurfaceManager clients

DesktopWidgetStore + Web widget process spawning
    -> WidgetSurfaceManager client

WallpaperPerformancePolicy
    -> PlaybackCoordinator (existing policy logic retained and expanded)

future screensaver logic
    -> ScreensaverCoordinator, not WallpaperEngine.cpp
```

## 12. First implementation slice

The first C++ refactor after this study is intentionally narrow and testable:

1. Add `DesktopShellHost.h/.cpp`.
2. Move shell discovery / raised-desktop detection / `0x052C` / WorkerW-bottom repair into it.
3. Add explicit `AttachSurface(..., DesktopSurfaceRole, desktopBounds, visible)` with verification.
4. Make the native wallpaper host converge on `DesktopShellHost` rather than owning an independent shell algorithm.
5. Make WebView2 child host windows layered/full-alpha before WebView2 controller creation when the active desktop composition requires it.
6. Replace Widget/Web z-order guessing with role-aware ordering.
7. Add diagnostics for mode, parent, layered style, visible state, WebView2 state and role.
8. Preserve existing Image/Video/Web/Scene behavior and ARM64 CI.

The real Windows acceptance test for this slice is:

```text
Start MiaoDesk
 -> wallpaper visible
 -> create desktop clock Widget
 -> Widget surface visibly appears above MiaoDesk wallpaper and below desktop icons
 -> open Settings/Search: Widget remains visible
 -> restart Explorer: wallpaper + Widget recover
 -> change display topology: both reattach to correct monitor geometry
```

Until the Widget is visibly rendered on a real Windows desktop, Widget runtime is not considered complete.
