# TuringDesk Desktop Completion Plan

Status: normative execution queue
Date: 2026-08-26
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

Current state: **implementation complete; real-Windows acceptance pending**

Tasks:

- [x] Remove remaining legacy Progman/WorkerW discovery and attachment ownership from `WallpaperEngine.cpp`.
- [x] Centralize `0x052C`, Raised Desktop detection, WorkerW discovery, parent validation and z-order repair in `DesktopShellHost` for all production paths.
- [x] Make native wallpaper, Web wallpaper and Widget production surfaces attach through the shared `DesktopShellHost` contract.
- [x] Centralize Explorer restart / stale HWND recovery, including generation-aware parent validation.
- [x] Validate mixed monitor geometry and negative virtual coordinates through the shared shell host and monitor-layout self-tests.
- [x] Add shell-mode and attachment diagnostics contract.
- [x] Add a build-time shell ownership guard so renderer/coordinator/Widget surfaces cannot rediscover Progman/WorkerW independently.
- [x] Physically remove the transitional `EnsureIndependentHostBounds` geometry mutation from `WallpaperWebRuntimeCoordinator.cpp` and delete its production bridge.

Validated implementation baseline:
- `1ed59a6a9c270408024e7143302a45592d2156a1`
- Native Windows x64 Source Validation `#298` completed successfully on 2026-08-25.
- Native Windows ARM64 `#690` completed successfully on 2026-08-25.

Current production state:
- `WallpaperEngine.cpp` directly owns a `DesktopShellHost` client and no longer contains Progman/WorkerW/DefView discovery, `0x052C`, SetParent, parent-client geometry mapping or WorkerW z-order helpers;
- the production WallpaperEngine compatibility wrapper no longer intercepts Windows shell APIs and remains only for M1 performance/automation persistence adapters;
- the Web/Widget coordinator is compiled directly and passes Independent host desktop-space geometry to `DesktopShellHost::EnsureSurface`;
- the coordinator production interception bridge is deleted and guarded from returning;
- geometry-only updates preserve existing visibility rather than implicitly showing hidden wallpapers;
- Explorer generation changes force reattachment through `EnsureSurface`, preventing recycled/stale parent HWNDs from being accepted accidentally.

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
- legacy shell helper implementations are physically removed;
- there is one Windows desktop attachment implementation, not parallel implementations in renderer/coordinator files;
- exact-head ARM64 is green;
- user-visible layering remains a real-Windows acceptance gate.

---

### M3 — Widget visible-runtime acceptance

Current state: **implementation in progress; real-Windows acceptance pending**

Goal: prove that a persisted Widget is actually rendered on the Windows desktop.

Tasks:

- [~] Complete Web Widget surface lifecycle through `DesktopShellHost`. Production attachment/recovery is centralized; real visible/recovery acceptance is still pending.
- [x] Surface WebView2 state: EnvironmentReady / ControllerReady / NavigationReady in the domain-owned per-surface health contract for the preferred Web child path.
- [x] Add configured/process/HWND/parent/style/z-order/visible/rendering-health state to `WidgetSurfaceHealth`; z-order interpretation is read-only shared desktop/shell telemetry while mutation remains in `DesktopShellHost`.
- [x] Add actionable error reporting to Widget UI and Pi through domain-owned `issueCode/detail/recommendedAction`.
- [x] Bind the five-phase acceptance round to one Widget identity set, one persisted placement configuration, one Windows session and one acceptance binary without treating runtime PID/HWND recreation as a failure.
- [x] Seal and independently verify explicit per-surface monitor/geometry/visibility/z-order health plus the baseline placement configuration hash/length.
- [x] Temporarily remove freeform Widget editing from the M3 product surface and ship three fixed clock showcase formats (`MinimalClock`, `DateClock`, `GlassClock`) with preset-owned geometry.
- [x] Make fixed-format automatic placement collision-aware across enabled Widgets on the same display and keep unrelated Widget records from disrupting the three-format showcase cycle.
- [x] Require the real-Windows acceptance executable to validate exactly one enabled `极简时钟`, `日期时钟`, and `玻璃时钟`, preset-owned sizes and same-monitor non-overlap before any durable phase cursor can advance.
- [x] Freeze fixed showcase identity and placement as config v2 (`id/title/monitorId/kind/x/y/width/height/zIndex/enabled`) across baseline -> settings -> search -> explorer -> monitor.
- [x] Add `verify-widget-product-model.ps1` to exact-head x64/ARM64 workflows so Small/Medium/Large, `SetSize`, `MoveToMonitor` or shell-attachment ownership cannot silently return during M3.
- [ ] Confirm Widget remains above TuringDesk wallpaper but below desktop icons on real Windows.
- [ ] Confirm Settings/Search windows do not hide or pause the Widget.
- [ ] Confirm Explorer restart restores Widget surfaces.
- [ ] Confirm monitor disconnect/reconnect restores Widget placement.

M3 implementation landed so far:
- `WidgetRuntimeHealth` is owned by `WidgetService` rather than UI/Pi reading private runtime diagnostics directly;
- `DesktopSnapshot` carries the same Widget runtime health alongside wallpaper state and Widget persistence state;
- Pi `wallpaper_state_get` consumes `DesktopSnapshot`, so AI and future UI/editor clients share one health contract;
- `WidgetSurfaceHealth` represents each enabled Web Widget with configured id, isolated PID/process-running state, HWND value/readiness, expected parent validity, `WS_CHILD` validity and current visibility;
- the preferred `WebDesktopSurfaceChild` publishes EnvironmentReady, ControllerReady and successful NavigationReady as HWND properties; the role property exists before async initialization, so not-ready stages are distinguished from telemetry absence;
- legacy child surfaces remain explicitly lifecycle-unreported instead of being guessed ready;
- M3 real-Windows acceptance is stricter than the compatibility health view: every accepted Web Widget must explicitly report Environment/Controller/Navigation telemetry and all three stages must be ready before the durable phase probe runs;
- Settings acceptance is pinned to visible class `TuringDesk.Native.DesktopLibrary` owned by `TuringDeskWallpaper.exe` in the same Windows session, and Search acceptance is pinned to visible class `TuringDesk.Native.SearchWindow` owned by `TuringDesk.exe` in that session;
- all extra fallible acceptance gates—fixed showcase validation, placement/identity continuity, product window/process context and strict lifecycle readiness—execute before `RunWidgetRuntimeAcceptanceProbe`, so an overall phase failure cannot occur after the durable phase cursor has already advanced;
- the M3 build-time contract guard locks those ordering/process/lifecycle/showcase invariants and rejects acceptance code regaining Widget-store or desktop-attachment ownership;
- shared `desktop/shell/DesktopSurfaceTelemetry.cpp` reports read-only z-order validity: icon DefView stays above TuringDesk surfaces and Widget surfaces stay above TuringDesk wallpaper surfaces; it contains no shell mutation APIs;
- `renderingHealthy` requires OS surface readiness, lifecycle readiness when reported, reported/valid z-order and the compatibility runtime diagnostic;
- aggregate `runtimeHealthy` requires a one-to-one structured/rendering-healthy surface set for all enabled Web Widgets;
- `WidgetService` now maps concrete failures to stable issue codes plus human-readable recommended actions; UI/Pi no longer infer remediation from Win32/WebView2 internals;
- production `DesktopWidgetUiAdapter` reads one `DesktopSnapshot`, keeps raw persistence items separate from temporary display items, and decorates enabled Widget list entries with health/action text without polluting stored titles;
- Pi `wallpaper_state_get` emits per-surface process/HWND/lifecycle/z-order/issue/action detail and `desktop_widget_list` reports matching runtime issue/action guidance;
- UI adapters/controllers and Pi remain forbidden from enumerating HWNDs or reading private runtime diagnostics directly; the Widget domain owns that translation;
- the current beginner surface intentionally uses fixed presets only: `极简时钟`, `日期时钟`, and `玻璃时钟`; drag/resize/edit-mode work is deferred until visible-runtime acceptance is proven;
- fixed preset placement scans normalized desktop space from the top-right, rejects candidates intersecting existing enabled Widgets plus the product gap, and falls back deterministically only when the display is too crowded to find a free slot;
- `scripts/verify-widget-product-model.ps1` guards the fixed-format/no-editor contract, the strict three-clock acceptance set and config-v2 identity continuity, and is run by both exact-head x64 and ARM64 workflows;
- `TuringDeskWidgetAcceptance.exe` and the phase runner enforce `baseline -> settings -> search -> explorer -> monitor`, exactly one of each fixed clock template at preset geometry without same-monitor overlap, same Widget identity/configuration, same interactive Windows session and the same acceptance binary;
- `WidgetAcceptanceConfigContinuity.cpp` reads enabled Web Widget configuration through `WidgetService::List` and freezes `id/title/monitorId/kind/x/y/width/height/zIndex/enabled` in `turingdesk.widget-acceptance-config.v2` across the five-phase round while intentionally allowing runtime PID/HWND recreation;
- the sealed evidence package now contains the baseline placement configuration checkpoint and manifest `placementConfig` SHA-256/length, and the independent verifier checks it again after sealing;
- the independent phase verifier explicitly requires every enabled Widget section to report `monitorValid=true`, `geometryValid=true`, `visible=true`, `zOrderValid=true` and `renderingHealthy=true` in every phase instead of trusting only an aggregate boolean;
- Settings/Search TuringDesk native foreground windows remain excluded from performance pause detection, and the M3 guard protects that invariant;
- architecture guards reject acceptance diagnostics regaining private Widget Store/INI or Shell HWND ownership, and native x64/ARM64 workflows are triggered by all M3 acceptance scripts/docs consumed by the build-time contract;
- `docs/WIDGET_RUNTIME_HEALTH_M3.md`, `docs/WIDGET_PLACEMENT_HEALTH_M3.md`, `docs/WIDGET_ACCEPTANCE_SEQUENCE_M3.md`, `docs/WIDGET_ACCEPTANCE_EVIDENCE_M3.md` and `docs/WIDGET_PRODUCT_MODEL_M3.md` describe the active health/product/evidence semantics and remaining acceptance work.

Real Windows acceptance flow:

```text
create the three fixed desktop clock formats
-> exactly one of each fixed format is enabled at preset-owned geometry
-> all three are visibly rendered without overlap
-> desktop icons remain usable
-> open TuringDesk settings
-> clocks remain visible
-> open TuringDesk search
-> clocks remain visible
-> restart Explorer
-> clocks return
-> reconnect/change display
-> unchanged Widget identity/title/placement configuration returns to correct monitor/geometry
-> record same-session human visual attestation
-> seal and independently verify the evidence package
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

**M3 — Widget visible-runtime implementation and acceptance.**

M1 is closed. M2 implementation is physically centralized in `DesktopShellHost` and the implementation baseline `1ed59a6a9c270408024e7143302a45592d2156a1` passed both x64 and ARM64 Windows validation; its real-Windows wallpaper/Widget/icon layering and recovery acceptance remains an outstanding gate and is explicitly carried into M3 acceptance. M3 now has domain-owned per-surface process/HWND/parent/style/visibility, preferred-child Environment/Controller/Navigation telemetry, shared read-only desktop/shell z-order telemetry, combined rendering-health semantics, stable actionable issue codes, production Widget list health/action display, Pi per-surface issue/action reporting through the shared snapshot, observed Settings/Search/Explorer/monitor evidence, same-session and same-binary continuity, config-v2 service-routed fixed showcase identity/placement continuity sealed into the final evidence manifest, Settings/Search acceptance pinned to the expected visible product class/process in the same session, and strict three-clock plus reported-and-ready lifecycle preconditions that are failure-atomic with respect to durable phase advancement. The M3 beginner product surface is deliberately narrowed to exactly one `极简时钟`, `日期时钟`, and `玻璃时钟` during acceptance, with preset-owned geometry and collision-safe automatic placement; drag/resize/monitor-edit APIs are deferred and guarded by `verify-widget-product-model.ps1` in both exact-head Windows workflows. The remaining gate is real ARM64 Widget visibility/icon-layer/Settings/Search/Explorer-restart/monitor-reconnect acceptance followed by same-session human visual attestation and sealed independent verification. M4 does not begin until that gate is satisfied.
