# TuringDesk wallpaper capability parity roadmap

Status: current implementation roadmap. This document is subordinate to `TURINGDESK-PRODUCT-BASELINE.md`; if they conflict, the product baseline wins.

Goal: evolve the native TuringDesk desktop subsystem into a polished Wallpaper Engine-class desktop engine, with equivalent functional depth where useful, while keeping TuringDesk branding, assets and implementation original. TuringDesk adds two first-class differentiators: AI desktop control and persistent desktop widgets.

## Implementation source-of-truth rule

**Wallpaper Engine is a product-capability benchmark only. It is not the engineering implementation reference.**

For Windows desktop mounting, WorkerW/Progman behavior, Windows 11 raised desktop, Web wallpaper hosting, multi-monitor lifecycle, pause/recovery and screensaver implementation, engineers must start from:

- `docs/LIVELY_CPP_WALLPAPER_IMPLEMENTATION.md`
- the public behavior of `rocksdanister/lively`
- Microsoft Windows / WebView2 / Media Foundation / DirectX documentation

Lively is GPL-3.0 while TuringDesk is MIT. TuringDesk therefore uses a clean-room-style native C++ reimplementation: study behavior/API sequences and edge cases, then independently implement them. Do not copy or mechanically translate Lively source.

When this roadmap says "Wallpaper Engine-class", it means **feature depth and user experience**, not source-code or implementation guidance.

## Target product model

```text
TuringDesk Desktop Composition
├─ Wallpaper Layer
│  ├─ Image
│  ├─ Video
│  ├─ Web
│  └─ Scene
├─ Widget Layer
│  ├─ Web widget (v1)
│  ├─ Native text / clock / image widgets
│  └─ data-bound / interactive widgets
└─ Control Layer
   ├─ Settings Center
   ├─ Scene / Widget Editor
   └─ TuringDesk AI Control API
```

The user should be able to do every normal wallpaper task manually. AI is an additional control surface, never the only way to operate the desktop.

## Current baseline

Already present on `main`:

- Windows 11 WorkerW / Progman desktop mounting with fallback diagnostics.
- Three native scenes: Aurora Flow, Neon Flow, Quiet Grid.
- Image wallpaper via WIC.
- Video wallpaper via Media Foundation.
- Enable / stop / resume and tray controls.
- Display-change reattachment and mount diagnostics.
- Per-monitor DPI awareness.
- Multi-monitor topology with Span / Clone / Primary-only / Independent layouts and negative-coordinate support.
- Stable monitor identity using DisplayConfig target device paths, with GDI fallback.
- Independent per-monitor Scene/image/video/Web assignments persisted across reconnect and reorder.
- Image Cover / Contain / Stretch / Center / Tile with focal alignment.
- Video Cover / Contain / Stretch / Center via MFPlay source crop/aspect policy.
- Adaptive performance policy with configurable FPS, fullscreen/maximized actions, Remote Desktop, battery saver, lock and idle handling.
- TuringDesk-owned UI windows are excluded from fullscreen/maximized wallpaper throttling decisions.
- Video loop, mute/volume, playback rate, seek/restart and bounded recovery synchronized across monitor surfaces.
- Persistent local wallpaper library with import, generated thumbnails, search, favorites, recent history and optional managed copies.
- Persistent Playlist / Schedule / Profile automation.
- Persistent per-executable wallpaper performance overrides.
- Isolated WebView2 Web wallpaper runtime with navigation and permission restrictions.
- Web wallpaper and Widget WebView2 surfaces have a desktop-surface runtime foundation, but raised-desktop visibility is still under active refactor against the Lively-based C++ implementation contract.
- `.tdwall` Web package creation and validation.
- Pi Agent can create validated Web `.tdwall` packages.
- Desktop widget persistence/runtime foundation: managed HTML widgets, normalized monitor-relative geometry, WebView2 overlay surfaces and AI CRUD interfaces.
- Desktop widget metadata is persisted through a Unicode-safe manifest, with migration of legacy ANSI manifests.
- A first Widgets page is available with list/create clock/enable-disable/delete/refresh operations.
- Desktop settings are constrained to the Windows monitor work area so the taskbar remains unobstructed.
- Pi Agent desktop control interfaces for reading desktop state, applying validated Web `.tdwall` packages, and creating/updating/removing/listing widgets.

## Baseline parity definition

Before calling the wallpaper subsystem "Wallpaper Engine-class" for normal daily use, the following user flow must be complete without requiring the editor:

```text
Library
  -> real thumbnail / current-use badge
  -> select wallpaper
  -> live preview
  -> inspect wallpaper properties
  -> choose monitor/layout
  -> apply
  -> playlist / schedule / per-app behavior
  -> reliable resume / recovery
```

Baseline parity also includes:

- image / video / Web / Scene playback;
- per-monitor and multi-monitor layouts;
- fullscreen/maximized/per-app performance behavior;
- playlists, schedules and profiles;
- screensaver integration;
- application rules that can react to foreground/running/fullscreen/maximized and playing-audio conditions;
- diagnostics that distinguish "configured", "process started", "surface visible" and "rendering healthy";
- widgets as an additional TuringDesk layer, not as a substitute for wallpaper parity.

## Delivery order

### P0 — desktop composition fundamentals

- [x] Multi-monitor topology and layout engine.
- [x] Performance / playback rules.
- [x] Persistent desktop widget store and Web widget overlay runtime foundation.
- [x] Unicode-safe Widget persistence and legacy ANSI migration.
- [x] Initial AI desktop control surface: state read, Web wallpaper apply, Widget CRUD.
- [x] Keep TuringDesk settings/search UI from accidentally pausing its own wallpaper runtime.
- [x] Keep Settings Center inside the Windows work area above the taskbar.
- [ ] Extract a dedicated `DesktopShellHost` and rebase WorkerW/Progman/raised-desktop attachment on the Lively-informed C++ implementation contract.
- [ ] Make Web wallpaper and Widget surfaces use the same verified desktop attachment/z-order service.
- [ ] Add raised-desktop style checks, including layered-surface requirements, parent validation and z-order repair.
- [ ] Add structured surface-health diagnostics: configured / process / HWND / parent / styles / z-order / WebView ready / visible / healthy.
- [ ] Complete scaling/alignment edge cases, including bounded video tile behavior.
- [ ] Complete video decoder capability diagnostics.
- [ ] Versioned Desktop Control API with atomic mutation, validation, undo/redo and change events.

### P1 — mature wallpaper-engine-class daily use

- [x] Wallpaper library backend.
- [x] Per-monitor independent wallpaper assignment.
- [x] Playlists, schedules and profiles.
- [x] Application rules foundation.
- [x] Basic Widgets page: list, create clock, enable/disable, delete and refresh.
- [ ] Rebuild the Installed page as real thumbnail cards with current-use badges, type chips and large previews.
- [ ] Add live preview before applying a wallpaper.
- [ ] Add a typed wallpaper property inspector for Image / Video / Web / Scene.
- [ ] Add monitor preview cards and make monitor names user-facing instead of exposing raw stable device IDs.
- [ ] Complete Widgets page with cards, live preview, monitor target, drag/move, resize and property inspector.
- [ ] Surface Widget runtime health in the UI: configured / process / HWND / WebView ready / visible / last error.
- [ ] Complete Web wallpaper isolation, navigation restrictions, crash recovery and visible-surface health checks.
- [ ] Add Playing Audio as an application-rule trigger.
- [ ] Add application-rule actions for loading Wallpaper / Playlist / Profile in addition to performance actions.
- [ ] Add Windows screensaver integration for current Wallpaper / Playlist / Profile.
- [ ] Reach mature daily-use settings depth for playback, monitor, application, performance and general behavior.

### P2 — creation and editing

- [ ] Strongly typed property system: sliders, toggles, colors, enums, text, vectors and resource references.
- [ ] Shared Project / Layer / Inspector / Timeline editor shell for Scene and Widget projects.
- [ ] Scene graph with Image / Text / Shape / Video / Web / Particle / Effect layers.
- [ ] Advanced GPU-backed native scene renderer with richer particles/effects and device-loss recovery.
- [ ] Keyframes, easing, layer parenting, transforms and reusable presets.
- [ ] Optional mouse/audio reactive wallpapers with explicit privacy controls.
- [ ] Smooth transitions between wallpapers and playlist entries.
- [ ] AI can inspect the typed project model, apply bounded edits, preview them, and undo its own changes.

### P3 — advanced parity and TuringDesk differentiation

- [ ] Shader/effect editor and safe shader compilation pipeline.
- [ ] 2D/3D scene support where the implementation cost is justified.
- [ ] Audio spectrum / beat / microphone data sources with explicit opt-in.
- [ ] Widget data connectors with permission-scoped providers and refresh policies.
- [ ] Optional interactive widgets; default remains click-through so desktop icons are never blocked.
- [ ] Virtual desktop awareness where Windows APIs allow it safely.
- [ ] HDR / color / mixed-DPI / mixed-refresh polish.
- [ ] Full Settings Center integration with monitor preview cards, live wallpaper preview, properties and performance controls.
- [ ] Reliability, last-known-good recovery and structured diagnostics.
- [ ] Portable `.tdwall` / `.tdwidget` import-export with metadata, versioning, hashes and safe extraction.

## AI contract

AI must never edit private INI files or internal runtime state directly as its long-term control model. The target architecture is a versioned TuringDesk Desktop Control API:

```text
Pi Agent
  -> typed TuringDesk desktop tools
  -> Desktop Control API
  -> validate mutation
  -> apply transaction
  -> renderer/widget runtime
  -> real result + state snapshot
```

Required long-term operations:

- `desktop_state_get`
- `wallpaper_apply`
- `wallpaper_properties_update`
- `playlist_update`
- `display_assignment_update`
- `performance_policy_update`
- `widget_create`
- `widget_update`
- `widget_remove`
- `widget_list`
- `desktop_undo`

The first implementation may use narrower tools, but all new code should migrate toward this control plane instead of adding unrelated one-off automation paths.

## Acceptance standard

A roadmap item is not considered complete until:

1. It works on ARM64 Windows 11 through the one-click deployment path.
2. Existing Scene/image/video/Web paths continue to work.
3. Configuration survives restart and relevant Windows state changes.
4. Failure produces an actionable diagnostic instead of silently hiding the problem.
5. Native self-test/CI coverage is updated when the behavior is testable without an interactive desktop.
6. A feature exposed to AI is also representable in typed state and can be validated before mutation.
7. User-facing functionality remains operable without AI.
8. UI wording distinguishes state persistence from actual runtime visibility; "enabled" alone is not considered proof that a Web/Widget surface is visible.
9. Windows desktop-shell work conforms to `docs/LIVELY_CPP_WALLPAPER_IMPLEMENTATION.md` rather than inventing a separate WorkerW/Progman strategy.
