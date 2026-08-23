# TuringDesk Wallpaper Engine parity roadmap

Status: current implementation roadmap. This document is subordinate to `TURINGDESK-PRODUCT-BASELINE.md`; if they conflict, the product baseline wins.

Goal: evolve the native TuringDesk wallpaper subsystem into a polished Wallpaper Engine-class desktop engine while keeping Search / AI isolated and lightweight.

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
- Independent per-monitor Scene/image/video assignments persisted across reconnect and reorder.
- Image Cover / Contain / Stretch / Center / Tile with focal alignment.
- Video Cover / Contain / Stretch / Center via MFPlay source crop/aspect policy.
- Adaptive performance policy with configurable FPS, fullscreen/maximized actions, Remote Desktop, battery saver, lock and idle handling.
- Video loop, mute/volume, playback rate, seek/restart and bounded recovery synchronized across monitor surfaces.
- Persistent local wallpaper library with import, generated thumbnails, search, favorites, recent history and optional managed copies.
- Persistent Playlist / Schedule / Profile automation.
- Persistent per-executable wallpaper performance overrides.

## Delivery order

### P0 — desktop engine fundamentals

- [x] Multi-monitor topology and layout engine.
- [ ] Complete scaling/alignment edge cases, including bounded video tile behavior.
- [x] Performance / playback rules.
- [ ] Complete video decoder capability diagnostics.

### P1 — Wallpaper Engine-class daily use

- [x] Wallpaper library.
- [x] Per-monitor independent wallpaper assignment.
- [x] Playlists, schedules and profiles.
- [x] Application rules.
- [ ] Complete Web wallpaper backend isolation, navigation restrictions and crash recovery.

### P2 — scene quality and interaction

- [ ] Strongly typed scene parameter system: sliders, toggles, colors, enums and text.
- [ ] Advanced GPU-backed native scene renderer with richer particles/effects and device-loss recovery.
- [ ] Optional mouse/audio reactive wallpapers with explicit privacy controls.
- [ ] Smooth transitions between wallpapers and playlist entries.

### P3 — Windows polish and productization

- [ ] Virtual desktop awareness where Windows APIs allow it safely.
- [ ] HDR / color / mixed-DPI / mixed-refresh polish.
- [ ] Full Settings Center integration with monitor preview cards, live wallpaper preview, properties and performance controls.
- [ ] Reliability, last-known-good recovery and structured diagnostics.
- [ ] Portable TuringDesk wallpaper package import/export with metadata, versioning and safe extraction.

## Acceptance standard

A roadmap item is not considered complete until:

1. It works on ARM64 Windows 11 through the one-click deployment path.
2. Existing Scene/image/video/Web paths continue to work.
3. Configuration survives restart and relevant Windows state changes.
4. Failure produces an actionable diagnostic instead of silently hiding the problem.
5. Native self-test/CI coverage is updated when the behavior is testable without an interactive desktop.
