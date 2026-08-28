# TuringDesk Widget Product Model (M3)

Status: normative product contract
Date: 2026-08-26

## User meaning

A TuringDesk Widget is a small information card attached to the Windows desktop. Normal users do not manage HWNDs, WebView2 processes, normalized coordinates, z-index values, or runtime attachment details.

## Current M3 simplification

The first visible-runtime phase intentionally removes freeform editing. There is no drag editor, resize handle, numeric x/y input, monitor reassignment editor, or Small/Medium/Large selector in the beginner surface.

The goal is to prove visual rendering and desktop layering first with a few fixed formats. Editing returns only after the fixed-format experience is visibly stable on real Windows.

## Fixed showcase formats

The current production showcase contains three distinct widget types:

- `玻璃时钟` — translucent glass time card with live clock;
- `今日待办` — task list card for daily productivity;
- `玻璃天气` — weather card with temperature and short forecast.

Each format owns its own fixed logical size and HTML/CSS appearance. Position remains automatic. Placement is collision-aware across enabled Widgets on the same display: the controller scans logical desktop space from the top-right toward the left and rejects candidate rectangles that intersect another enabled Widget plus the product gap. Raw coordinates remain an implementation detail and are not exposed to normal users.

The existing `＋ 新建桌面小组件` entry is intentionally kept simple during M3. Repeated creation balances the three fixed formats so a tester can add three Widgets and compare real desktop rendering without opening an editor. Unrelated Widget records do not change which fixed format comes next.

## Allowed management actions

The Widget page currently exposes only the operations needed for visible-runtime validation:

```text
add fixed format
hide / show
delete
refresh runtime state
```

Freeform editing is explicitly deferred.

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
- fixed preset-owned visual geometry instead of public editing APIs;
- collision-safe automatic placement in normalized display space;
- balanced production `CreateClock` selection across the three fixed showcase formats;
- no public `SetSize` / `MoveToMonitor` controller editing surface during this phase;
- runtime generation and pause decisions decoupled from Wallpaper Enabled/host visibility;
- `TuringDeskWidgetAcceptance.exe` now requires exactly one enabled `玻璃时钟`, `今日待办`, and `玻璃天气`, verifies their preset-owned sizes, and rejects same-monitor overlap before any phase cursor can advance;
- a single arbitrary Web Widget can no longer satisfy M3 real-Windows acceptance;
- `scripts/verify-widget-product-model.ps1` guards both the fixed-format/no-editor controller contract and the strict three-widget acceptance set, while preventing the controller from regaining shell attachment ownership;
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
-> all three occupy non-overlapping automatic placements on each display
-> all remain above TuringDesk wallpaper and below desktop icons
-> settings window does not hide or pause them
-> search window does not hide or pause them
-> Explorer restart restores them
-> monitor reconnect restores the same persisted placement configuration
```

The acceptance executable rejects extra enabled Web Widgets during this M3 round so evidence cannot accidentally describe a different product configuration. Runtime PID/HWND recreation remains allowed; persisted Widget identity, placement configuration, Windows session, phase order, preferred WebView2 lifecycle readiness, geometry/monitor visibility and z-order health remain continuous evidence requirements.

Only after this fixed-format path is stable should drag/resize/edit-mode work resume.

M3 remains open until the real ARM64 Windows visible-runtime acceptance passes. This product contract does not replace that gate.
