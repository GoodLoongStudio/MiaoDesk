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
- [ ] Run Repo Hygiene.
- [ ] Run Windows x64 build.
- [ ] Confirm no regressions in existing Scene D2D renderer tests.

## Exit criteria
- Generated Scene package auto-loads and animates in the preview pane.
- Pause freezes the current frame and Resume continues from the paused time.
- Reload re-reads files without requiring a new AI generation.
- Fullscreen uses the same preview renderer, can be exited with Esc, and resizes correctly.
- A preview load/render error is visible in the preview pane and Reload remains available.
- Both wallpaper and widget creator modes share the same behavior.
