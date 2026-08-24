# TuringDesk Desktop domain architecture

Status: normative architecture contract.
Date: 2026-08-25

This document defines module boundaries for the TuringDesk desktop product. It exists to prevent UI, AI, wallpaper renderers and Windows Shell integration from growing into one coupled subsystem.

## 1. Target process architecture

```text
TuringDesk.exe
├─ AppShell
├─ Search
├─ AI / Pi Runtime
├─ Settings UI
└─ DesktopControlClient / DesktopControlService
            │
            │ stable intent contract
            ▼
TuringDeskWallpaper.exe
├─ DesktopShell
├─ Wallpaper
├─ Widgets
├─ Automation
└─ Performance

TuringDeskHarness.exe
└─ 高级工作台
```

This is intentionally a small-process architecture, not microservices. Internal modules have strong boundaries; process count stays small.

## 2. Dependency rule

All product clients use one control path:

```text
UI / Pi / future Editor
        ↓
UI adapter/controller or Pi adapter
        ↓
Desktop Control contract
        ↓
Domain service
        ↓
Store / Runtime / Renderer
        ↓
DesktopShellHost when a Windows desktop surface is required
```

Forbidden long-term paths:

```text
UI -> wallpaper.ini
UI -> DesktopWidgetStore
UI -> WorkerW / Progman
UI -> WebView2 runtime process
Pi adapter -> wallpaper.ini
Pi adapter -> DesktopWidgetStore
Pi adapter -> ShellExecute wallpaper runtime
Renderer -> AI runtime
Wallpaper -> Pi runtime
```

During migration, legacy paths may remain only when the replacement path is not yet available. New code must not add another direct path.

## 3. Domains

### 3.1 DesktopShell

Owns only Windows desktop infrastructure:

- Progman / WorkerW / SHELLDLL_DefView discovery
- Windows 11 Raised Desktop
- Explorer restart recovery
- surface attachment and z-order
- display topology needed for desktop attachment

It knows surface roles such as Wallpaper and Widget. It does not know Aurora, playlists, AI, widget HTML or library metadata.

Primary implementation: `DesktopShellHost`.

### 3.2 Wallpaper

Owns:

- wallpaper state and package validation through `WallpaperService`
- wallpaper library and packages
- Image / Video / Web / Scene selection
- wallpaper renderer lifecycle
- per-monitor assignment
- scaling and content properties

Wallpaper code does not call Pi.

### 3.3 Widgets

Owns:

- Widget persistence through `WidgetService`
- package/source management
- normalized geometry
- Widget runtime lifecycle
- Widget surface management

`DesktopWidgetStore` is persistence, not a public product API. UI and AI must use `DesktopControlService` or an approved controller rather than mutate the Store directly.

### 3.4 Automation

Owns:

- playlists
- schedules
- profiles
- application rules

Automation produces desktop intents; it does not directly manipulate WorkerW or renderer HWNDs.

### 3.5 Performance

Owns policy inputs and decisions:

- fullscreen / maximized
- app rules
- battery / saver
- Remote Desktop
- lock / idle
- Normal / Throttle / Pause / Stop

Renderers consume the resulting policy; renderers do not independently rediscover system policy.

### 3.6 AI

Owns:

- Pi Runtime
- Provider / Model state
- native tool registration
- conversational orchestration

AI is a client of Desktop Control. It does not own wallpaper or Widget persistence.

### 3.7 UI

UI responsibilities are deliberately narrow:

- render navigation, cards, preview and inspector
- collect user intent
- call a controller/service
- display returned state/errors

UI does not implement domain rules.

## 4. Desktop Control facade and domain services

The current concrete boundary is:

- `DesktopControlService` — shared facade for product clients
- `WallpaperService` — wallpaper state/package ownership
- `WidgetService` — Widget CRUD/persistence ownership
- `DesktopWidgetController` — direct UI controller for new Widget UI code
- `DesktopWidgetUiAdapter` — transitional compatibility adapter for the current production legacy library window

Current facade responsibilities:

```text
GetState
ApplyWebPackage
CreateWebWidget
UpdateWidget
RemoveWidget
ListWidgets
EnsureRuntime
```

Current or staged clients:

- Pi native desktop tool adapter (`DesktopWidgetTools.cpp`)
- production legacy Widget UI through `WallpaperLibraryWindowProduction.cpp -> DesktopWidgetUiAdapter -> DesktopControlService`
- new Widget UI through `DesktopWidgetController`
- Desktop Library V2 / Widget UI
- future Scene / Widget Editor

The existing Pi tool names are an adapter protocol and are not the domain API itself.

## 5. Adapter rule

`DesktopWidgetTools.cpp` is an adapter only:

```text
Pi JSON arguments
    ↓ parse
DesktopControlService request
    ↓ execute
DesktopControlResult
    ↓ format
Pi text result
```

It must not regain persistence/runtime ownership.

`DesktopWidgetController` follows the same rule for new Win32 UI:

```text
Win32 Widget action
    ↓
DesktopWidgetController
    ↓
DesktopControlService
    ↓
WidgetService
```

It must never include or instantiate `DesktopWidgetStore`.

The current production `WallpaperLibraryWindow.cpp` is a large legacy source file. To avoid a risky mechanical rewrite while V2 is still incomplete, production no longer compiles that file directly. `WallpaperLibraryWindowProduction.cpp` compiles the implementation through `DesktopWidgetUiAdapter`, which preserves the old call shape but routes Widget list/create/update/remove operations through `DesktopControlService`. This bridge is temporary and must be removed when V2 reaches parity.

Similarly, `WallpaperLibraryWindowV2.cpp` should become:

```text
Win32 input
    ↓
Library/Widget controller
    ↓
DesktopControlService
```

rather than another giant business-logic window.

## 6. UI migration rule

The current production `WallpaperLibraryWindow.cpp` behavior stays active until V2 reaches functional parity, but its production Widget CRUD path is now service-routed through `WallpaperLibraryWindowProduction.cpp` and `DesktopWidgetUiAdapter`.

V2 may replace it only after these capabilities are preserved:

- wallpaper library/search/import/apply
- Widget CRUD and runtime state
- playlists
- displays
- application rules
- performance
- 图灵 AI model/API configuration
- Pi capability and native desktop tools
- 高级工作台 entry

A visual redesign is never allowed to remove a product capability.

## 7. Next refactor slices

1. Replace the transitional `DesktopWidgetUiAdapter` bridge with direct `DesktopWidgetController` use when the V2 production window reaches feature parity.
2. Expand `WallpaperService` from Web package application into library item application and monitor assignment.
3. Make Desktop Library V2 depend on controllers/services only.
4. Separate Automation UI from automation persistence and execution through `AutomationService`.
5. Move Performance UI and remaining engine settings ownership through `PerformanceService`.
6. Remove legacy direct shell attachment from `WallpaperEngine.cpp` after `DesktopShellHost` is sole owner.
7. Introduce a versioned transactional Desktop Control contract with events and undo/redo.

## 8. Completion tests

Architecture work is not complete because files were renamed. It is complete when behavior crosses the same boundary from multiple clients.

Required tests over time:

```text
Pi creates Widget -> UI lists same Widget
UI moves Widget -> Pi reads updated geometry
UI applies wallpaper -> Pi reads same current state
Pi applies wallpaper -> UI shows same current state
Explorer restarts -> runtime recovers without UI/AI special handling
```

The domain service owns the state transition; clients only express intent.
