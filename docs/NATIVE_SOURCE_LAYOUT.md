# MiaoDesk native source layout

Status: normative engineering structure contract.
Date: 2026-09-01

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
└─ include/miaodesk/
```

Repository-level read-only product defaults live under `config/` and are
installed as `Config/`. They are not mutable user state.

## Ownership rules

- `app/` owns executable composition and application startup only.
- `ai/pi/` owns Pi runtime integration; `ai/tools/` owns product/native tool adapters; `ai/agent/` owns Direct fallback agent implementation.
- `desktop/control/` owns the shared Desktop Control facade.
- `desktop/shell/` is the only Windows desktop attachment owner: Progman, WorkerW, raised desktop, Explorer recovery, surface parent/z-order.
- `desktop/wallpaper/` owns wallpaper state and runtime. Renderer-specific code lives below `render/`, `web/`, `monitor/`, `library/` or `runtime/`.
- `desktop/wallpaper/legacy/` is migration-only by intent but currently load-bearing. No new product behavior may be added there; the exit is decomposition, not expansion.
- `desktop/widgets/`, `desktop/automation/` and `desktop/performance/` own their domain services/runtime state.
- `ui/wallpaper/`, `ui/automation/`, `ui/performance/`, `ui/ai/`, `ui/search/` and `ui/settings/` own presentation/intent translation only.
- `harness/` owns the Advanced Workbench process/runtime bridge.
- `search/` owns native search/index integration shared by executables.

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
