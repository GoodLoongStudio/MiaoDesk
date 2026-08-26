# TuringDesk Widget Product Model (M3)

Status: normative product contract
Date: 2026-08-26

## User meaning

A TuringDesk Widget is a small information card attached to the Windows desktop. Normal users do not manage HWNDs, WebView2 processes, normalized coordinates, z-index values, or runtime attachment details.

## Current M3 simplification

The first visible-runtime phase intentionally removes freeform editing. There is no drag editor, resize handle, numeric x/y input, monitor reassignment editor, or Small/Medium/Large selector in the beginner surface.

The goal is to prove visual rendering and desktop layering first with a few fixed formats. Editing returns only after the fixed-format experience is visibly stable on real Windows.

## Fixed showcase formats

The current production showcase contains three clock formats:

- `极简时钟` — compact single-line time card;
- `日期时钟` — time plus full date card;
- `玻璃时钟` — larger translucent/glass visual treatment.

Each format owns its own fixed logical size and HTML/CSS appearance. Position remains automatic: Widgets start near the top-right of the selected display and stack downward, then continue in columns to the left.

The existing `＋ 新建桌面时钟` entry is intentionally kept simple during M3. Repeated creation rotates through the three fixed formats so a tester can add three Widgets and compare real desktop rendering without opening an editor.

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

- `WidgetFixedPreset::{MinimalClock, DateClock, GlassClock}` in `DesktopWidgetController`;
- fixed preset-owned visual geometry instead of public editing APIs;
- automatic top-right placement with downward stacking and leftward overflow columns;
- production `CreateClock` rotating through the three fixed showcase formats;
- no public `SetSize` / `MoveToMonitor` controller editing surface during this phase;
- runtime generation and pause decisions decoupled from Wallpaper Enabled/host visibility;
- x64/ARM64 exact-head workflows triggered by source-layout/domain/shell/UI-parity guards and this product contract.

These are implementation milestones only. They do not satisfy the real-Windows visual gate by themselves.

## Fixed-format acceptance

The first acceptance pass is deliberately small:

```text
click create three times
-> 极简时钟 visibly renders
-> 日期时钟 visibly renders
-> 玻璃时钟 visibly renders
-> all remain above TuringDesk wallpaper and below desktop icons
-> hide/show works
-> delete works
-> restart keeps enabled Widgets
-> Explorer restart restores them
```

Only after this fixed-format path is stable should drag/resize/edit-mode work resume.

M3 remains open until the real ARM64 Windows visible-runtime acceptance passes. This product contract does not replace that gate.
