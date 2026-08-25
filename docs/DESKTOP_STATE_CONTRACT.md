# Desktop State Contract

## Purpose

TuringDesk must expose one caller-facing view of the active desktop so the production UI, Pi-first Turing AI tools, and the future editor cannot observe different wallpaper and Widget state.

## Contract

`DesktopControlService::GetSnapshot` is the cross-domain read boundary.

```text
UI / Pi / future Editor
        ↓
DesktopControlService::GetSnapshot
        ↓
┌────────────────────┬──────────────────┐
│ WallpaperService   │ WidgetService    │
│ WallpaperState     │ Widget list      │
└────────────────────┴──────────────────┘
        ↓
DesktopSnapshot
```

`DesktopSnapshot` contains the normalized wallpaper-facing `DesktopState` plus the complete persisted Widget list. `DesktopState::widgetCount` is derived from that same Widget list instead of being read independently.

Legacy `DesktopControlService::GetState` remains for compatibility, but it delegates to `GetSnapshot`. New UI, Pi tools that need both wallpaper and Widget information, and the future editor should prefer `GetSnapshot`.

## Ownership rules

- `DesktopControlService` coordinates cross-domain reads; it does not own INI or Widget persistence.
- `WallpaperService` remains the owner of wallpaper state persistence and package application.
- `WidgetService` remains the owner of Widget persistence and mutations.
- UI and Pi must not compose direct `WallpaperService` + `DesktopWidgetStore` reads themselves.
- A visual redesign must consume the same snapshot contract rather than create a parallel state model.

## Current limitation

The snapshot is a coordinated process-local read of the two domain services, not yet a transactional/versioned runtime snapshot with event sequencing. The later Desktop Control contract should add version, events and undo/redo so long-running editor sessions can detect concurrent changes.

## Acceptance examples

```text
Pi creates Widget -> UI refreshes GetSnapshot -> Widget is present
UI disables Widget -> Pi reads GetSnapshot -> enabled=false
UI applies wallpaper -> Pi reads GetSnapshot -> same current wallpaper
Pi applies wallpaper -> library/editor reads GetSnapshot -> same current wallpaper
```

Real Windows validation is still required for Explorer shell recovery, Widget Z-order/visibility and end-to-end refresh timing; CI success alone does not close those acceptance items.
