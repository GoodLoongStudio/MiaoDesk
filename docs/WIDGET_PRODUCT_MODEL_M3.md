# TuringDesk Widget Product Model (M3)

Status: normative product contract
Date: 2026-08-26

## User meaning

A TuringDesk Widget is a small information card attached to the Windows desktop. Normal users do not manage HWNDs, WebView2 processes, normalized coordinates, z-index values, or runtime attachment details.

## Creation contract

The beginner creation flow asks only:

1. which Widget to add;
2. which display to use;
3. size preset: Small / Medium / Large.

Position is automatic. The first Widget starts near the top-right of the selected display with a logical margin. Additional Widgets stack downward and then continue in columns to the left. Raw x/y/width/height remain domain persistence details, not beginner UI fields.

The first production template is `桌面时钟`. Its default size is Medium.

## Size presets

Presets are logical proportions of the target display and remain DPI/resolution independent:

- Small: compact glance card;
- Medium: default clock card;
- Large: expanded card.

The controller translates presets into persisted normalized geometry. UI, Pi, and a future editor must not duplicate that translation.

## Placement and display changes

Changing display re-runs automatic placement on the destination display. It must not require a user to type coordinates. Changing size preserves the Widget identity and updates its persisted geometry through the shared Desktop control/domain path.

## Normal and edit modes

Normal desktop mode shows only the Widget content. Selection borders, resize handles and placement controls belong to a future explicit desktop edit mode, not normal rendering.

The management UI may expose `编辑 / 隐藏 / 删除 / 重新显示`, while implementation diagnostics remain behind a details surface.

## Runtime independence

An enabled Widget is an independent desktop surface. Its existence must not semantically depend on whether a dynamic wallpaper is enabled. Wallpaper and Widget share `DesktopShellHost` attachment/layering infrastructure, but they are separate product states.

Expected visual order remains:

```text
desktop icons
Widget
TuringDesk wallpaper
```

## First-template acceptance

The desktop clock is the reference lifecycle:

```text
add
-> automatically placed on selected display
-> visibly rendered
-> change Small / Medium / Large
-> move to another display
-> hide/show
-> delete
-> survives TuringDesk restart
-> recovers after Explorer restart
-> recovers after monitor reconnect
```

M3 remains open until the real ARM64 Windows visible-runtime acceptance passes. This product contract does not replace that gate.
