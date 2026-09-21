# MiaoDesk native source layout

Status: normative engineering structure contract.
Date: 2026-09-01
Updated: 2026-09-20 — registered `content/input/` (Scene input bus) and `tests/`; added the "Keeping this contract current" rule and the reverse-direction CI guard.

Native implementation is organized directly under `src/` by process/domain. `src/` is the source module root; `native` and a second nested `src` layer are intentionally not used.

## Physical layout

```text
src/
├─ app/
│  └─ main.cpp
├─ ai/
│  ├─ pi/
│  ├─ tools/
│  └─ agent/
├─ content/
│  ├─ asset/
│  ├─ binding/
│  ├─ model/
│  ├─ package/
│  ├─ render/
│  ├─ runtime/
│  ├─ scene/
│  ├─ serialization/
│  └─ shader/
├─ search/
├─ harness/
├─ desktop/
│  ├─ control/
│  ├─ demo/
│  ├─ preview/
│  ├─ shell/
│  ├─ wallpaper/
│  │  ├─ runtime/
│  │  ├─ legacy/
│  │  ├─ library/
│  │  ├─ monitor/
│  │  ├─ render/
│  │  └─ web/
│  ├─ widgets/
│  ├─ automation/
│  └─ performance/
├─ ui/
│  ├─ search/
│  ├─ settings/
│  ├─ ai/
│  ├─ wallpaper/
│  ├─ automation/
│  └─ performance/
├─ tests/
└─ include/miaodesk/
```

Repository-level read-only product defaults live under `config/` and are
installed as `Config/`. They are not mutable user state.

## Ownership rules

- `app/` owns executable composition and application startup only.
- `ai/pi/` owns Pi runtime integration; `ai/tools/` owns product/native tool adapters; `ai/agent/` owns Direct fallback agent implementation.
- `content/` owns the MiaoDesk Content Framework and nothing else: `model/` (ContentDefinition / ContentInstance / parameter resolution), `package/` (`.mdwidget` / `.mdwall` ingress, catalog, managed lifecycle, uninstall), `scene/` + `serialization/` (scene object model and its JSON form), `render/` (D2D / D3D11 backends, post-process, render graph), `runtime/` (scene runtime and frame scheduler), `binding/` (declarative data binding and the capability broker), `asset/` (asset database), `shader/` (shader contract and GPU parameter blocks), `input/` (Scene input bus channel contract and the platform-independent audio / pointer analysis that feeds it; Windows capture itself lives in `desktop/`). Host-facing glue stays in `desktop/` and `ui/`; `content/` must not own Windows Shell attachment, z-order, or widget persistence.
- `desktop/control/` owns the shared Desktop Control facade.
- `desktop/shell/` is the only Windows desktop attachment owner: Progman, WorkerW, raised desktop, Explorer recovery, surface parent/z-order.
- `desktop/wallpaper/` owns wallpaper state and runtime. Renderer-specific code lives below `render/`, `web/`, `monitor/`, `library/` or `runtime/`.
- `desktop/wallpaper/legacy/` is migration-only by intent but currently load-bearing. No new product behavior may be added there; the exit is decomposition, not expansion.
- `desktop/widgets/`, `desktop/automation/` and `desktop/performance/` own their domain services/runtime state.
- `ui/wallpaper/`, `ui/automation/`, `ui/performance/`, `ui/ai/`, `ui/search/` and `ui/settings/` own presentation/intent translation only.
- `harness/` owns the Advanced Workbench process/runtime bridge.
- `search/` owns native search/index integration shared by executables.
- `tests/` owns regression and acceptance test sources. Tests are not a product domain: they must not be added to a product executable's source list, and they never own product behavior.

## Keeping this contract current

This document is normative, not historical. When a change adds, removes, or renames a top-level source domain, the same change must update this document and the canonical domain list in `scripts/verify-path-layout-contract.ps1`.

`scripts/verify-path-layout-contract.ps1` enforces the reverse direction too: every directory under `src/` must be a registered canonical domain and must be named in this document. A new undocumented domain therefore fails CI rather than silently drifting. This guard exists because `src/content/` — the Content Framework, 23 implementation files across nine sub-domains — was added without being registered here, leaving the layout contract describing a tree that no longer existed.

## Headers

Existing shared headers remain under `include/miaodesk/` so `#include <miaodesk/...>` stays stable. This avoids a mass include rewrite that would add risk without improving runtime behavior. New target-local headers should live beside their implementation; move a header into `include/miaodesk/` only when it is genuinely shared across domains or process targets.

## Build graph rule

`src/CMakeLists.txt` mirrors the physical tree. It is forbidden to reintroduce `src/native/`, a second nested source root, or implementation `.cpp` files directly under `src/`.

Sources shared by the product executables are compiled through the typed static
library targets `MiaoDeskCore` and `MiaoDeskHarnessCore`. A `.cpp` file must have
one build owner rather than being repeated in multiple executable source lists.

The path-layout contract enforces the canonical source domains and rejects the obsolete `src/native` container.

## Migration rule

Moving a file is not permission to change behavior. Source-layout commits should be mechanically reviewable: same blobs where possible, CMake/script/documentation path updates, then exact-head Windows CI. Business refactors continue only after the layout wave is green.
