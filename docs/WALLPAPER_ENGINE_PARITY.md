# TuringDesk Wallpaper Engine parity roadmap

Status: current implementation roadmap. This document is subordinate to `TURINGDESK-PRODUCT-BASELINE.md`; if they conflict, the product baseline wins.

Goal: evolve the native TuringDesk desktop subsystem into a polished Wallpaper Engine-class desktop engine, with equivalent functional depth where useful, while keeping TuringDesk branding, assets and implementation original. TuringDesk adds two first-class differentiators: AI desktop control and persistent desktop widgets.

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
- Video loop, mute/volume, playback rate, seek/restart and bounded recovery synchronized across monitor surfaces.
- Persistent local wallpaper library with import, generated thumbnails, search, favorites, recent history and optional managed copies.
- Persistent Playlist / Schedule / Profile automation.
- Persistent per-executable wallpaper performance overrides.
- Isolated WebView2 Web wallpaper runtime with navigation and permission restrictions.
- `.tdwall` Web package creation and validation.
- Pi Agent can create validated Web `.tdwall` packages.
- Desktop widget persistence/runtime foundation: managed HTML widgets, normalized monitor-relative geometry, WebView2 overlay surfaces and AI CRUD interfaces.
- Pi Agent desktop control interfaces for reading desktop state, applying validated Web `.tdwall` packages, and creating/updating/removing/listing widgets.

## Delivery order

### P0 — desktop composition fundamentals

- [x] Multi-monitor topology and layout engine.
- [x] Performance / playback rules.
- [x] Persistent desktop widget store and Web widget overlay runtime.
- [x] Initial AI desktop control surface: state read, Web wallpaper apply, Widget CRUD.
- [ ] Complete scaling/alignment edge cases, including bounded video tile behavior.
- [ ] Complete video decoder capability diagnostics.
- [ ] Versioned Desktop Control API with atomic mutation, validation, undo/redo and change events.

### P1 — Wallpaper Engine-class daily use

- [x] Wallpaper library backend.
- [x] Per-monitor independent wallpaper assignment.
- [x] Playlists, schedules and profiles.
- [x] Application rules.
- [ ] Rebuild the Installed page as real thumbnail cards with current-use badges, type chips and large previews.
- [ ] Add live preview and property inspector before applying a wallpaper.
- [ ] Add a first-class Widgets page with cards, enable/disable, monitor target, move/resize and delete.
- [ ] Complete Web wallpaper/backend isolation, navigation restrictions and crash recovery.
- [ ] Match the useful daily-use settings depth of Wallpaper Engine: playback, monitor, application, performance and general behavior.

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
