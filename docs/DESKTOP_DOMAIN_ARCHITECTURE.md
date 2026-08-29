# MiaoDesk Desktop domain architecture

Status: normative architecture contract.
Date: 2026-08-25

This document defines module boundaries for the MiaoDesk desktop product. It exists to prevent UI, AI, wallpaper renderers and Windows Shell integration from growing into one coupled subsystem.

The physical implementation layout is defined by `docs/NATIVE_SOURCE_LAYOUT.md`. Domain boundaries and physical folders must agree; `src/native/src/` is a module root, not a flat implementation bucket.

## 1. Target process architecture

```text
MiaoDesk.exe
├─ AppShell
├─ Search
├─ AI / Pi Runtime
├─ Settings UI
└─ DesktopControlClient / DesktopControlService
            │
            │ stable intent contract
            ▼
MiaoDeskWallpaper.exe
├─ DesktopShell
├─ Wallpaper
├─ Widgets
├─ Automation
└─ Performance

MiaoDeskHarness.exe
└─ 高级工作台
```

This is intentionally a small-process architecture, not microservices. Internal modules have strong boundaries; process count stays small.

Current implementation roots:

```text
src/native/src/
├─ app/
├─ ai/
│  ├─ pi/
│  ├─ tools/
│  └─ agent/
├─ search/
├─ harness/
├─ desktop/
│  ├─ control/
│  ├─ shell/
│  ├─ wallpaper/
│  ├─ widgets/
│  ├─ automation/
│  └─ performance/
└─ ui/
```

Public C++ headers remain under `src/native/include/miaodesk/` during the current migration so implementation movement does not silently change the API/include contract.

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
UI -> WallpaperAutomationStore
UI -> WorkerW / Progman
UI -> WebView2 runtime process
UI -> runtime HWND enumeration
Pi adapter -> wallpaper.ini
Pi adapter -> DesktopWidgetStore
Pi adapter -> ShellExecute wallpaper runtime
Pi adapter -> runtime HWND enumeration
Renderer -> AI runtime
Wallpaper -> Pi runtime
```

During migration, legacy paths may remain only behind an explicit production bridge when the replacement path is not yet complete. New code must not add another direct path.

## 3. Domains

### 3.1 DesktopShell

Physical implementation: `src/native/src/desktop/shell/`.

Owns only Windows desktop infrastructure:

- Progman / WorkerW / SHELLDLL_DefView discovery
- Windows 11 Raised Desktop
- Explorer restart recovery
- surface attachment and z-order
- display topology needed for desktop attachment
- shared read-only surface z-order interpretation through `DesktopSurfaceTelemetry`

It knows surface roles such as Wallpaper and Widget. It does not know Aurora, playlists, AI, widget HTML or library metadata.

Primary mutation implementation: `DesktopShellHost`. `DesktopSurfaceTelemetry` is read-only and must never gain `SetParent`, `SetWindowPos`, WorkerW discovery or repair ownership.

### 3.2 Wallpaper

Physical implementation: `src/native/src/desktop/wallpaper/`.

Owns:

- wallpaper state and package validation through `WallpaperService`
- wallpaper library and packages
- Image / Video / Web / Scene selection
- wallpaper renderer lifecycle
- per-monitor assignment
- scaling and content properties

Subfolders separate `library`, `monitor`, `render`, `web`, `runtime` and migration-only `legacy` code. Wallpaper code does not call Pi.

### 3.3 Widgets

Physical implementation: `src/native/src/desktop/widgets/` with UI adapters under `src/native/src/ui/widgets/`.

Owns:

- Widget persistence through `WidgetService`
- package/source management
- normalized geometry
- Widget runtime lifecycle
- Widget surface management
- caller-facing aggregate health through `WidgetRuntimeHealth`
- caller-facing per-Web-Widget runtime/surface health through `WidgetSurfaceHealth`

`DesktopWidgetStore` is persistence, not a public product API. UI and AI must use `DesktopControlService` or an approved controller rather than mutate the Store directly. Runtime diagnostics may be produced by the wallpaper process, but UI/Pi must not read private INI diagnostics, inspect WebView2 child properties or enumerate runtime HWNDs directly.

M3 now structures each enabled Web Widget as configured/process/PID/HWND/parent/child-style/visibility/WebView2 lifecycle/z-order state behind `WidgetService::GetRuntimeHealth`:

- `WebDesktopSurfaceChild` publishes EnvironmentReady, ControllerReady and successful NavigationReady as process-safe HWND properties;
- the child role property is published before asynchronous WebView2 initialization, so not-ready lifecycle stages are distinguished from telemetry absence;
- legacy child surfaces remain explicitly unreported rather than being guessed ready;
- `DesktopSurfaceTelemetry` supplies read-only shared z-order semantics: icon DefView remains above MiaoDesk surfaces and Widget surfaces remain above MiaoDesk wallpaper surfaces;
- `WidgetService` consumes these sources and computes `renderingHealthy`; it does not mutate shell state.

The active contract is documented in `docs/WIDGET_RUNTIME_HEALTH_M3.md`.

### 3.4 Automation

Physical implementation: `src/native/src/desktop/automation/` with UI compatibility code under `src/native/src/ui/automation/`.

Owns:

- playlists
- schedules
- profiles
- application rules
- persisted active playlist / last matched schedule state
- manual next-playlist state transition
- runtime evaluation through `AutomationService`

`WallpaperAutomationStore` is persistence/execution infrastructure, not a UI API. `AutomationService` owns persistence/evaluation access. `AutomationUiAdapter` is the temporary compatibility surface for the current Win32 automation UI.

Automation produces desktop intents; it does not directly manipulate WorkerW or renderer HWNDs.

### 3.5 Performance

Physical implementation: `src/native/src/desktop/performance/` with UI compatibility code under `src/native/src/ui/performance/`.

Owns policy inputs and decisions:

- fullscreen / maximized
- app rules
- battery / saver
- Remote Desktop
- lock / idle
- Normal / Throttle / Pause / Stop

`PerformanceService` owns persisted performance-policy configuration. `PerformanceUiAdapter` is the UI-facing compatibility boundary; performance controls use it instead of owning persistence directly.

Renderers consume the resulting policy; renderers do not independently rediscover system policy.

### 3.6 AI

Physical implementation: `src/native/src/ai/`.

Owns:

- Pi Runtime
- Provider / Model state
- native tool registration
- conversational orchestration

`ai/tools/DesktopWidgetTools.cpp` is the Pi-to-Desktop-Control adapter; it is deliberately outside the Widget persistence domain. AI is a client of Desktop Control. It does not own wallpaper or Widget persistence. `wallpaper_state_get` reads `DesktopSnapshot`, including `WidgetRuntimeHealth`, instead of composing a separate AI-only runtime view. Pi must not enumerate Widget processes/HWNDs itself; per-surface state is supplied by the Widget domain through the snapshot contract.

### 3.7 UI

Physical implementation: `src/native/src/ui/`.

UI responsibilities are deliberately narrow:

- render navigation, cards, preview and inspector
- collect user intent
- call a controller/service
- display returned state/errors

UI does not implement domain rules. Production compatibility bridges for the old library/automation windows live under their UI domains and are migration-only.

## 4. Desktop Control facade and domain services

The current concrete boundary is:

- `DesktopControlService` — shared facade for product clients
- `WallpaperService` — wallpaper state/package/library apply/per-monitor assignment ownership
- `WidgetService` — Widget CRUD/persistence/runtime-health ownership
- `AutomationService` — playlist/profile/schedule persistence, evaluation and manual playlist transition ownership
- `PerformanceService` — performance-policy persistence ownership
- `DesktopWidgetController` — direct UI controller for new Widget UI code
- `DesktopWidgetUiAdapter` — transitional compatibility adapter for the current production legacy library window
- `AutomationUiAdapter` — transitional compatibility adapter for the current automation window
- `PerformanceUiAdapter` — UI-facing adapter for performance settings while the legacy settings surface is migrated

Current DesktopControl facade responsibilities include:

```text
GetState / GetSnapshot
  -> DesktopState
  -> Widget list
  -> WidgetRuntimeHealth
     -> WidgetSurfaceHealth[]
ApplyWebPackage
ApplyLibraryItem
AssignLibraryItemToMonitor
ClearMonitorAssignment
CreateWebWidget
UpdateWidget
RemoveWidget
ListWidgets
EnsureRuntime
```

Current or staged clients:

- Pi native desktop tool adapter (`src/native/src/ai/tools/DesktopWidgetTools.cpp`)
- production legacy Widget UI through `ui/wallpaper/WallpaperLibraryWindowProduction.cpp -> DesktopWidgetUiAdapter -> DesktopControlService`
- new Widget UI through `DesktopWidgetController`
- automation UI/runtime through `AutomationUiAdapter -> AutomationService`
- performance UI through `PerformanceUiAdapter -> PerformanceService`
- Desktop Library V2 / Widget UI
- future Scene / Widget Editor

The existing Pi tool names are an adapter protocol and are not the domain API itself.

## 5. Adapter rule

`DesktopWidgetTools.cpp` is an adapter only:

```text
Pi JSON arguments
    ↓ parse
DesktopControlService request / DesktopSnapshot
    ↓ execute/read
DesktopControlResult + domain-owned runtime health
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

It must never instantiate `DesktopWidgetStore` or inspect runtime HWNDs.

The current production `WallpaperLibraryWindow.cpp` is a large legacy source file under `ui/wallpaper/`. Production does not compile it directly. `WallpaperLibraryWindowProduction.cpp` compiles the implementation through `DesktopWidgetUiAdapter`, which preserves the old call shape but routes Widget list/create/update/remove operations through `DesktopControlService`. This bridge is temporary and must be removed when V2 reaches parity.

Automation follows the same migration rule:

```text
Legacy Automation Window action
    ↓
AutomationUiAdapter
    ↓
AutomationService
    ↓
WallpaperAutomationStore
```

`AutomationUiAdapter` may preserve the old store-shaped method names for migration, but it must not instantiate `WallpaperAutomationStore`, read INI files, or own scheduling rules. Runtime evaluation is also routed through `AutomationService`.

Performance follows the same migration rule:

```text
Legacy Performance controls
    ↓
PerformanceUiAdapter
    ↓
PerformanceService
    ↓
performance persistence
```

`PerformanceUiAdapter` only translates UI intent and errors. It must not call Win32 profile APIs itself.

Similarly, `WallpaperLibraryWindowV2.cpp` must become:

```text
Win32 input
    ↓
Library/Widget controller
    ↓
DesktopControlService
```

rather than another giant business-logic window.

## 6. UI migration rule

The current production `WallpaperLibraryWindow.cpp` behavior stays active until V2 reaches functional parity, but its production Widget CRUD path is service-routed through `WallpaperLibraryWindowProduction.cpp` and `DesktopWidgetUiAdapter`.

V2 may replace it only after these capabilities are preserved:

- wallpaper library/search/import/apply
- Widget CRUD and runtime state
- playlists
- displays
- application rules
- performance
- 妙喵 AI model/API configuration
- Pi capability and native desktop tools
- 高级工作台 entry

A visual redesign is never allowed to remove a product capability.

## 7. Current remaining refactor slices

Completed or substantially landed:

- source implementation is physically grouped by process/domain instead of flat `src/native/src/*.cpp`;
- Wallpaper library item apply and monitor assignment are behind `WallpaperService/DesktopControlService`;
- Automation UI/runtime evaluation routes through `AutomationUiAdapter/AutomationService`;
- Performance UI compatibility routes through `PerformanceUiAdapter/PerformanceService`;
- Widget UI and Pi adapters route through Desktop Control rather than Widget persistence;
- M2 production shell ownership is physically centralized in `DesktopShellHost`; exact-head `1ed59a6a9c270408024e7143302a45592d2156a1` passed x64 and ARM64 Windows validation;
- M3 exposes `WidgetRuntimeHealth` inside `DesktopSnapshot` and Pi reads the same snapshot contract;
- M3 carries per-enabled-Web-Widget process/PID, HWND, parent, child-style and visibility state as `WidgetSurfaceHealth`;
- M3 consumes the preferred Web child Environment/Controller/Navigation lifecycle properties without allowing UI/Pi to inspect them directly;
- M3 shares read-only z-order interpretation from `desktop/shell/DesktopSurfaceTelemetry.cpp`, while all mutation stays in `DesktopShellHost`;
- `renderingHealthy` now requires OS surface readiness, preferred-child lifecycle readiness when reported, valid reported z-order and the compatibility runtime diagnostic.

Remaining order:

1. Preserve M2 real-Windows wallpaper/Widget/icon layering and Explorer-recovery acceptance as an outstanding gate.
2. Expose actionable per-surface lifecycle/z-order/runtime failures in production Widget UI and richer Pi state output through the existing snapshot contract.
3. Complete real Widget visibility, icon-layer, Settings/Search, Explorer restart and monitor reconnect acceptance on ARM64 Windows.
4. Make Desktop Library V2 depend on controllers/services only and reach functional parity before production switch.
5. Replace transitional legacy UI bridges after V2 parity.
6. Introduce a versioned transactional Desktop Control contract with events and undo/redo.
7. Split public/private headers and CMake library targets only when that change improves enforceable dependency boundaries; do not churn include paths merely for cosmetics.

## 8. Completion tests

Architecture work is not complete because files were renamed. It is complete when behavior crosses the same boundary from multiple clients.

Required tests over time:

```text
Pi creates Widget -> UI lists same Widget
UI moves Widget -> Pi reads updated geometry
UI/Pi read the same WidgetRuntimeHealth + WidgetSurfaceHealth from DesktopSnapshot
Widget surface reports process/HWND/Environment/Controller/Navigation/z-order/visible health through WidgetService
UI applies wallpaper -> Pi reads same current state
Pi applies wallpaper -> UI shows same current state
Automation UI edits playlist -> runtime evaluates the same persisted playlist
Performance UI changes fullscreen action -> runtime consumes the same persisted policy
Explorer restarts -> runtime recovers without UI/AI special handling
```

The domain service owns the state transition; clients only express intent.
