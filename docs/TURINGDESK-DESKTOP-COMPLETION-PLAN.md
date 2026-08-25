# TuringDesk Desktop Completion Plan

Status: normative execution queue
Date: 2026-08-25
Branch policy: formal delivery goes to `main` only

This plan converts the current desktop backlog into a strict completion queue. Work should proceed milestone by milestone instead of expanding multiple unfinished areas in parallel.

## Completion rule

A milestone is complete only when all of the following are true:

1. implementation is on `main`;
2. the exact `main` SHA passes the relevant ARM64 CI/self-tests;
3. architecture guards/documentation are updated where the contract changed;
4. no previously working Pi / wallpaper / Widget / automation / performance capability regresses;
5. user-visible functionality that requires an interactive desktop is verified on real Windows before it is called complete.

`Code written`, `build succeeds`, `CI succeeds`, and `mock succeeds` are intermediate states, not final product completion.

## Fixed execution order

### M0 — Baseline and backlog lock

Current state: **complete**

Deliverables:
- Pi-first AI runtime remains the only normal AI path.
- Desktop capability benchmark and Lively-based Windows implementation reference are documented separately.
- Desktop domain architecture is documented.
- ARM64 exact-SHA CI is green before advancing.

Exit gate:
- current `main` has a green Native Windows ARM64 run.

---

### M1 — Finish domain ownership and remove remaining direct state paths

Current state: **complete**

Goal: finish the structural refactor before the visual rewrite so new UI does not become another business-logic monolith.

Tasks, in order:

- [x] Route legacy Performance controls completely through `PerformanceUiAdapter -> PerformanceService`.
- [x] Remove direct performance-policy INI ownership from the production legacy engine/UI path where the service replacement exists.
- [x] Finish production automation UI migration through `AutomationUiAdapter -> AutomationService`.
- [x] Ensure automation runtime consumes the same persisted state written through `AutomationService`.
- [x] Expand `WallpaperService` from Web-only package application to normal library-item application.
- [x] Move per-monitor wallpaper assignment intent behind the Wallpaper domain service.
- [x] Keep `DesktopControlService` as the small shared facade while domain rules remain in Wallpaper/Widget/Automation/Performance services.
- [x] Add/extend architecture guards that reject new UI/Pi direct access to private stores/INI/runtime launching.

Validated baseline:
- `fa5316972d2ed4936b06bc0acb67eb42e763ebb5`
- Native Windows ARM64 run `#653` completed successfully on 2026-08-25.

Exit gate:

```text
UI / Pi
  -> adapter/controller
  -> DesktopControlService or domain service
  -> domain state transition
  -> runtime
```

No newly supported operation may require UI or Pi to edit `wallpaper.ini`, `DesktopWidgetStore`, `WallpaperAutomationStore`, or shell HWNDs directly.

---

### M2 — Make `DesktopShellHost` the sole Windows desktop attachment owner

Current state: **in progress**

Tasks:

- [ ] Remove remaining legacy Progman/WorkerW discovery and attachment ownership from `WallpaperEngine.cpp`.
- [ ] Centralize `0x052C`, Raised Desktop detection, WorkerW discovery, parent validation and z-order repair in `DesktopShellHost`.
- [ ] Make native wallpaper, Web wallpaper and Widget surfaces all attach through the same contract.
- [ ] Centralize Explorer restart / stale HWND recovery.
- [ ] Validate mixed monitor geometry and negative virtual coordinates through the shared shell host.
- [x] Add shell-mode and attachment diagnostics contract.
- [x] Add a build-time shell ownership guard so renderer/coordinator/Widget surfaces cannot rediscover Progman/WorkerW independently.

Required diagnostics:

```text
shellMode
parentValid
layeredRequired
layeredApplied
zOrderValid
visible
lastError
```

Exit gate:
- there is one Windows desktop attachment implementation, not parallel implementations in renderer/coordinator files.

---

### M3 — Widget visible-runtime acceptance

Current state: **highest user-facing runtime risk**

Goal: prove that a persisted Widget is actually rendered on the Windows desktop.

Tasks:

- [ ] Complete Web Widget surface lifecycle through `DesktopShellHost`.
- [ ] Surface WebView2 state: EnvironmentReady / ControllerReady / NavigationReady.
- [ ] Add configured/process/HWND/parent/style/z-order/visible/rendering-health state.
- [ ] Add actionable error reporting to Widget UI.
- [ ] Confirm Widget remains above TuringDesk wallpaper but below desktop icons.
- [ ] Confirm Settings/Search windows do not hide or pause the Widget.
- [ ] Confirm Explorer restart restores Widget surfaces.
- [ ] Confirm monitor disconnect/reconnect restores Widget placement.

Real Windows acceptance flow:

```text
create desktop clock
-> clock is visibly rendered
-> desktop icons remain usable
-> open TuringDesk settings
-> clock remains visible
-> restart Explorer
-> clock returns
-> reconnect/change display
-> clock returns to correct monitor/geometry
```

Exit gate:
- real ARM64 Windows passes the flow above.

---

### M4 — Replace the old desktop settings layout with the new product shell

Current state: **not production-ready**

Goal: ship a Lively-inspired information architecture while preserving TuringDesk functionality and original implementation/branding.

Target navigation:

```text
图灵智能桌面
├─ 壁纸
│  ├─ 库
│  ├─ 当前桌面
│  └─ 播放列表
├─ 小组件
├─ 自动化
├─ 性能
├─ 图灵 AI
└─ 设置
```

Tasks:

- [ ] Replace horizontal giant Tab layout with navigation shell.
- [ ] Add top title/search/command area.
- [ ] Preserve wallpaper import/search/apply.
- [ ] Preserve Widget management.
- [ ] Preserve Playlist / displays / application rules / performance.
- [ ] Preserve 图灵 AI Provider/Model/Base URL/API Key UI.
- [ ] Preserve Pi Agent capability.
- [ ] Preserve 高级工作台 entry.
- [ ] Add functional parity guard so V2 cannot remove an existing feature again.
- [ ] Remove the transitional legacy production wrapper only after parity is verified.

Exit gate:
- new UI is production UI and all old product capabilities remain reachable.

---

### M5 — Mature Wallpaper Library and current-desktop experience

Tasks:

- [ ] Real thumbnail card grid.
- [ ] Current-use badge.
- [ ] Wallpaper type chips.
- [ ] Search/filter/favorite/recent polish.
- [ ] Large wallpaper detail/preview view.
- [ ] Live preview before apply.
- [ ] Friendly monitor cards instead of raw stable device IDs.
- [ ] Apply to global/current layout or a selected monitor.
- [ ] Clear runtime/apply status and errors.

Exit gate:

```text
Library
-> select card
-> live preview
-> choose target monitor/layout
-> apply
-> current-use state updates correctly
```

---

### M6 — Complete Widget product UX

Tasks:

- [ ] Widget card/grid UI.
- [ ] Live Widget preview.
- [ ] Friendly monitor target selection.
- [ ] Drag/move on desktop or editor surface.
- [ ] Resize.
- [ ] Enable/disable/delete.
- [ ] Widget property inspector.
- [ ] Widget runtime-health panel.
- [ ] `.tdwidget` metadata/versioning/import-export foundation.
- [ ] Keep default Widget mode click-through so desktop icons remain usable.

Exit gate:
- a normal user can create, position, resize, inspect and remove Widgets without AI.

---

### M7 — Typed Wallpaper / Widget property system

Goal: create one property model shared by manual UI, AI and future editors.

Types:
- slider/number;
- toggle;
- color;
- enum/combo;
- text;
- vector/position;
- resource reference;
- display condition/group metadata.

Tasks:

- [ ] Define typed property schema and validation.
- [ ] Add property snapshots to Desktop Control state.
- [ ] Add property update transactions.
- [ ] Build common Inspector controls.
- [ ] Map Image properties.
- [ ] Map Video properties.
- [ ] Map Web properties.
- [ ] Map Scene properties.
- [ ] Map Widget properties.
- [ ] Expose safe bounded property edits to Pi.
- [ ] Add change events and undo/redo foundation.

Exit gate:
- manual UI and Pi modify the same typed property state and see the same result.

---

### M8 — Automation, application rules and screensaver parity

Tasks:

- [ ] Playing Audio application-rule trigger.
- [ ] Rule actions: load Wallpaper / Playlist / Profile.
- [ ] Finish foreground/running/fullscreen/maximized rule combinations.
- [ ] Mature Playlist/Schedule/Profile UI.
- [ ] Screensaver presentation runtime.
- [ ] Screensaver source: current Wallpaper / Playlist / Profile.
- [ ] Per-monitor/span/duplicate screensaver behavior.
- [ ] Input-to-exit and failure black-screen fallback.

Exit gate:
- automation state written by UI is the same state executed by runtime and visible to AI.

---

### M9 — Editor foundation

Goal: establish one reusable editor architecture before adding advanced effects.

Tasks:

- [ ] Project model.
- [ ] Layer/scene graph.
- [ ] Project / Layer / Inspector / Timeline shell.
- [ ] Image layer.
- [ ] Text layer.
- [ ] Shape layer.
- [ ] Video layer.
- [ ] Web layer.
- [ ] Transform/parenting model.
- [ ] Save/load/versioned project format.
- [ ] Preview runtime integration.
- [ ] AI bounded edits + preview + undo.

Exit gate:
- a user can create and save a basic multi-layer Scene/Widget project and apply it to the desktop.

---

### M10 — Animation and visual effects

Tasks, in this exact order:

- [ ] Timeline.
- [ ] Keyframes.
- [ ] Easing.
- [ ] Reusable animation presets.
- [ ] Particle layer/runtime.
- [ ] Particle editor.
- [ ] Effect/material system.
- [ ] Safe Shader compilation pipeline.
- [ ] Shader/effect editor.
- [ ] Smooth wallpaper/playlist transitions.

Exit gate:
- visual creation workflows no longer require hand-written package code.

---

### M11 — Audio reactive and advanced scene capabilities

Tasks:

- [ ] Desktop audio spectrum source.
- [ ] Beat/event source.
- [ ] Explicit opt-in microphone source.
- [ ] Audio-reactive properties/particles/effects.
- [ ] 2D/3D scene runtime decision and minimal supported model.
- [ ] Camera/transforms where 3D is justified.
- [ ] GPU/device-loss recovery.
- [ ] HDR/mixed-DPI/mixed-refresh polish.

Exit gate:
- advanced features meet performance/privacy/recovery requirements and do not destabilize normal wallpaper playback.

---

### M12 — Consumer packaging and update path

Tasks:

- [ ] Standalone Windows installer.
- [ ] No Git requirement.
- [ ] No `gh` requirement.
- [ ] No GitHub login requirement.
- [ ] Public release/update channel.
- [ ] Safe upgrade/rollback.
- [ ] Uninstall cleanup without destroying user content unexpectedly.
- [ ] ARM64 release path.
- [ ] x64 release path where required by product scope.

Exit gate:
- a normal Windows user can install, update and uninstall TuringDesk without developer tooling.

---

### M13 — Final end-to-end acceptance

Required full product flow:

```text
install TuringDesk
-> launch
-> top AI/search entry works
-> configure model
-> Pi conversation works
-> apply Image wallpaper
-> apply Video wallpaper
-> apply Web wallpaper
-> switch native Scene
-> multi-monitor assignment works
-> create/move/resize Widget
-> Widget survives Settings + Explorer restart
-> create Playlist/Schedule/Profile
-> application/performance rules take effect
-> screensaver works
-> edit typed properties manually
-> Pi edits the same typed properties
-> undo/redo works
-> create/edit a Scene project
-> restart Windows/TuringDesk
-> persisted state and surfaces recover
-> update product
-> state remains valid
```

Only after this flow and the relevant failure/recovery scenarios pass on real Windows may the desktop system be called complete.

## Execution policy

- Work one milestone at a time.
- Within a milestone, finish the listed tasks in dependency order.
- Do not spend a full iteration polishing a later milestone while an earlier exit gate is still failing.
- Small opportunistic fixes are allowed only when they prevent a regression or unblock the active milestone.
- Every meaningful wave lands directly on `main` and is checked against the exact-head ARM64 workflow.
- Keep Pi functionality intact during every UI/runtime refactor.
- Do not claim user-visible completion from CI alone.

## Current active milestone

**M2 — Make `DesktopShellHost` the sole Windows desktop attachment owner.**

M1 is closed on the green `fa531697...` baseline. M2 now owns all Progman/WorkerW/desktop-parent migration work. After M2, proceed immediately to **M3 Widget real visibility**, then **M4 new production UI**.