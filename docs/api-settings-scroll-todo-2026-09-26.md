# API Settings Scroll TODO — 2026-09-26

Scope: `DesktopAiSettingsPage` on small windows, high DPI, and reduced vertical space.

## P0 — Usability
- [ ] Make the API settings panel vertically scrollable when the form is taller than the viewport.
- [ ] Add horizontal scrolling only when adaptive layout still cannot fit the minimum content width.
- [ ] Support mouse wheel vertical scrolling.
- [ ] Support Shift + mouse wheel and horizontal wheel for horizontal scrolling.
- [ ] Keep all editable controls and bottom actions reachable at small resolutions / high DPI.
- [ ] Recompute scroll ranges after resize and DPI changes.
- [ ] Clamp scroll offsets after the viewport grows so the page never stays stranded off-screen.
- [ ] Keep model dropdown, API key controls, status box, and action buttons aligned while scrolling.

## P1 — Validation
- [ ] Add a lightweight source-contract test for scroll styles/messages/range bookkeeping.
- [ ] Run Repo Hygiene.
- [ ] Run Windows x64 Build.
- [ ] Run Windows ARM64 Package.

## Exit criteria
- On a short window, the page shows a vertical scrollbar and the user can reach Image API Key and all bottom actions.
- On a narrow window below the minimum layout width, a horizontal scrollbar appears.
- On a sufficiently large window, unnecessary scrollbars disappear automatically.
- Mouse wheel and scrollbar thumb dragging both update child controls and the custom-drawn background consistently.
