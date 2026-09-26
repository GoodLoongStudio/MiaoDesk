# API Settings Scroll TODO — 2026-09-26

Scope: `DesktopAiSettingsPage` on small windows, high DPI, and reduced vertical space.

## P0 — Usability
- [x] Make the API settings panel vertically scrollable when the form is taller than the viewport.
- [x] Add horizontal scrolling only when adaptive layout still cannot fit the minimum content width.
- [x] Support mouse wheel vertical scrolling.
- [x] Support Shift + mouse wheel and horizontal wheel for horizontal scrolling.
- [x] Keep all editable controls and bottom actions reachable at small resolutions / high DPI.
- [x] Recompute scroll ranges after resize and DPI changes.
- [x] Clamp scroll offsets after the viewport grows so the page never stays stranded off-screen.
- [x] Keep model dropdown, API key controls, status box, and action buttons aligned while scrolling.

## P1 — Validation
- [x] Add a lightweight source-contract test for scroll styles/messages/range bookkeeping.
- [x] Run Repo Hygiene.
- [x] Run Windows x64 Build.
- [x] Run Windows ARM64 Package.

## Build evidence (SHA `9dc2f288506563ecf8e5f32b111c6388e88593bf`, checked 2026-09-27)

| Workflow | Run | Result | Relevant steps |
| --- | --- | --- | --- |
| Repo Hygiene | [#209](https://github.com/GoodLoongStudio/MiaoDesk/actions/runs/36256953987) | success | `Verify API settings scrolling contract` |
| Windows x64 Build | [#515](https://github.com/GoodLoongStudio/MiaoDesk/actions/runs/36256953990) | success | `Build`, `Verify staged GlassClock Content Framework route` |
| Windows ARM64 Package | [#240](https://github.com/GoodLoongStudio/MiaoDesk/actions/runs/36256954003) | success | `package` + `installer` jobs |

Every job and step in these three workflows reported `success` for this SHA. The
single non-success step in `Windows x64 Build` — `Upload Content widget lifecycle
diagnostics` — is guarded by `if: failure()` and is therefore skipped precisely
because the lifecycle test passed; it is not a failure and not an unrun check.

Note the split recorded by `TODO.md` BASE-02: CI green here means the build and
source contracts pass. It does **not** stand in for the real-Windows scroll
behavior in the exit criteria below, which still needs a physical device.

## Exit criteria
- On a short window, the page shows a vertical scrollbar and the user can reach Image API Key and all bottom actions.
- On a narrow window below the minimum layout width, a horizontal scrollbar appears.
- On a sufficiently large window, unnecessary scrollbars disappear automatically.
- Mouse wheel and scrollbar thumb dragging both update child controls and the custom-drawn background consistently.
