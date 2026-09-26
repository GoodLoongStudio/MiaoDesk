# Preview Sandbox TODO — 2026-09-26

Scope: AI wallpaper editor + AI widget editor (shared `ContentCreatorDialog`).

## P0 — Preview sandbox
- [x] Add explicit preview state (empty/loading/playing/paused/static/error).
- [x] Add Play/Pause control for Scene preview without destroying renderer state.
- [x] Add Reload control that revalidates and reloads the generated package from disk.
- [x] Add Fullscreen preview mode with Esc to exit.
- [x] Add keyboard shortcuts in fullscreen: Space = Play/Pause, R = Reload.
- [x] Surface preview errors inside the preview canvas instead of relying on modal dialogs.
- [x] Keep static `manifest.preview` fallback working.
- [x] Keep “Regenerate” available alongside preview sandbox controls.
- [x] Apply the same sandbox behavior to `.mdwall` and `.mdwidget`.

## P1 — Validation
- [x] Extend content creator contract test for sandbox controls/state/fullscreen.
- [x] Run Repo Hygiene.
- [x] Run Windows x64 build.
- [x] Confirm no regressions in existing Scene D2D renderer tests.

## Build evidence (SHA `9dc2f288506563ecf8e5f32b111c6388e88593bf`, checked 2026-09-27)

| Workflow | Run | Result | Relevant steps |
| --- | --- | --- | --- |
| Repo Hygiene | [#209](https://github.com/GoodLoongStudio/MiaoDesk/actions/runs/36256953987) | success | `Verify dedicated content creator modes` |
| Windows x64 Build | [#515](https://github.com/GoodLoongStudio/MiaoDesk/actions/runs/36256953990) | success | `Render a textured sprite through the real D2D backend`, `Verify binding response curves`, `Verify scene runtime binding end to end`, `Verify Widget lifecycle in quick-build layout` |
| Windows ARM64 Package | [#240](https://github.com/GoodLoongStudio/MiaoDesk/actions/runs/36256954003) | success | `package` + `installer` jobs |

Every job and step in these workflows reported `success` for this SHA. The one
non-success step in `Windows x64 Build` — `Upload Content widget lifecycle
diagnostics` — is guarded by `if: failure()`, so skipping it means the lifecycle
test passed. It is not a failure and not an unexecuted check.

The D2D renderer tests genuinely executed and passed, so "no regressions in
existing Scene D2D renderer tests" is backed by CI rather than assumed. The
device-facing exit criteria below (frame-accurate pause/resume, fullscreen
resize, error visibility) still require a real Windows session.

## Exit criteria
- Generated Scene package auto-loads and animates in the preview pane.
- Pause freezes the current frame and Resume continues from the paused time.
- Reload re-reads files without requiring a new AI generation.
- Fullscreen uses the same preview renderer, can be exited with Esc, and resizes correctly.
- A preview load/render error is visible in the preview pane and Reload remains available.
- Both wallpaper and widget creator modes share the same behavior.
