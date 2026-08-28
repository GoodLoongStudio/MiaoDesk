# TuringDesk Widget Product Model (M3)

Status: normative product contract
Date: 2026-08-28

## User meaning

A TuringDesk Widget is a small information card attached to the Windows desktop. Normal users do not manage HWNDs, WebView2 processes, normalized coordinates, z-index values, or runtime attachment details.

## Current M3 simplification

M3 keeps fixed visual templates and a beginner-friendly surface. Users can **drag widgets on the desktop** to reposition them; positions persist across sessions. Resize handles, numeric x/y editors, monitor reassignment editors, and Small/Medium/Large selectors remain deferred.

The goal is to prove visual rendering, desktop layering, and drag persistence on real Windows before richer editing returns.

## Fixed showcase formats

The current production showcase contains three distinct widget types:

- `玻璃时钟` — translucent glass time card with live clock;
- `今日待办` — task list card for daily productivity;
- `玻璃天气` — weather card with temperature and short forecast.

Each format owns its own fixed logical size and HTML/CSS appearance. **Initial placement** is automatic and collision-aware across enabled Widgets on the same display: the controller scans logical desktop space from the top-right toward the left and rejects candidate rectangles that intersect another enabled Widget plus the product gap. After creation, users may drag a widget anywhere within the same monitor's normalized bounds. Raw coordinates remain an implementation detail in settings UI.

The existing `＋ 新建桌面小组件` entry lets users pick a fixed format or auto-rotate the next preset.

## Desktop drag behavior

Enabled Web Widget surfaces expose a native drag layer (`WebDesktopSurfaceChild`):

- drag starts from the widget's top grip / drag handle;
- movement updates the live HWND position immediately;
- release persists normalized `x/y` through `WidgetService::Update`;
- cancel restores the pre-drag placement;
- drag does not change preset-owned `width/height`.

`DesktopWidgetController::MoveTo` is the controller-facing API for programmatic moves using the same update path.

## Allowed management actions

The Widget page currently exposes:

```text
add fixed format
drag on desktop to reposition
hide / show
delete
refresh runtime state
```

Resize and monitor reassignment editors remain deferred.

## Runtime independence

An enabled Widget is an independent desktop surface. Its existence must not semantically depend on whether a dynamic wallpaper is enabled. Wallpaper and Widget share `DesktopShellHost` attachment/layering infrastructure, but they are separate product states.

The production Web/Widget coordinator therefore keeps two separate runtime decisions:

- Web wallpaper requests still honor Wallpaper `Enabled` and wallpaper-host visibility;
- Widget requests are generated from enabled Widget state regardless of Wallpaper `Enabled`;
- wallpaper-host invisibility may pause Web wallpaper rendering, but it must not pause enabled Widgets by itself;
- shared Performance `Pause` / `Stop` policy may still pause both domains when the policy itself requires that behavior.

`scripts/verify-desktop-domain-contract.ps1` guards this independence so the Widget runtime cannot silently regain Wallpaper Enabled/visibility coupling.

Expected visual order remains:

```text
desktop icons
Widget
TuringDesk wallpaper
```

## Landed implementation

The M3 product path now includes:

- `WidgetFixedPreset::{GlassClock, TodayTasks, WeatherGlass}` in `DesktopWidgetController`;
- fixed preset-owned visual geometry instead of public resize APIs;
- collision-safe automatic placement for newly created widgets;
- balanced production `CreateClock` selection across the three fixed showcase formats;
- desktop drag repositioning with persisted normalized coordinates;
- `DesktopWidgetController::MoveTo` for service-routed placement updates;
- no public `SetSize` / `MoveToMonitor` controller editing surface during this phase;
- runtime generation and pause decisions decoupled from Wallpaper Enabled/host visibility;
- `TuringDeskWidgetAcceptance.exe` requires exactly one enabled `玻璃时钟`, `今日待办`, and `玻璃天气`, verifies their preset-owned sizes, and rejects same-monitor overlap at the initial baseline checkpoint;
- a single arbitrary Web Widget can no longer satisfy M3 real-Windows acceptance;
- `scripts/verify-widget-product-model.ps1` guards the fixed-format controller contract, drag persistence path, and strict three-widget acceptance set while preventing the controller from regaining shell attachment ownership;
- x64/ARM64 exact-head workflows run the Widget product guard in addition to source-layout/domain/shell contracts.

These are implementation milestones only. They do not satisfy the real-Windows visual gate by themselves.

## Fixed-format acceptance

The first acceptance pass is deliberately small:

```text
click create three times
-> exactly one 玻璃时钟 is enabled at preset size
-> exactly one 今日待办 is enabled at preset size
-> exactly one 玻璃天气 is enabled at preset size
-> all three visibly render
-> all three occupy non-overlapping automatic placements on each display at baseline
-> user drag may reposition widgets after baseline without invalidating preset-owned sizes
-> all remain above TuringDesk wallpaper and below desktop icons
-> settings window does not hide or pause them
-> search window does not hide or pause them
-> Explorer restart restores them
-> monitor reconnect restores the same persisted placement configuration
```

The acceptance executable rejects extra enabled Web Widgets during this M3 round so evidence cannot accidentally describe a different product configuration. Runtime PID/HWND recreation remains allowed; persisted Widget identity, placement configuration, Windows session, phase order, preferred WebView2 lifecycle readiness, geometry/monitor visibility and z-order health remain continuous evidence requirements.

Resize/edit-mode work may resume only after this fixed-format + drag path is stable on real Windows.

M3 remains open until the real ARM64 Windows visible-runtime acceptance passes. This product contract does not replace that gate.
