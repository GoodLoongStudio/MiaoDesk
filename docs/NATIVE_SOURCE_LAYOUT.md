# TuringDesk native source layout

Status: normative engineering structure contract.
Date: 2026-08-25

The native implementation is organized by process/domain. `src/native/src` is a module root, not a bucket for unrelated `.cpp` files.

## Physical layout

```text
src/native/
├─ src/
│  ├─ app/
│  │  └─ main.cpp
│  ├─ ai/
│  │  ├─ pi/
│  │  ├─ tools/
│  │  └─ agent/
│  ├─ search/
│  ├─ harness/
│  ├─ desktop/
│  │  ├─ control/
│  │  ├─ shell/
│  │  ├─ wallpaper/
│  │  │  ├─ runtime/
│  │  │  ├─ legacy/
│  │  │  ├─ library/
│  │  │  ├─ monitor/
│  │  │  ├─ render/
│  │  │  └─ web/
│  │  ├─ widgets/
│  │  ├─ automation/
│  │  └─ performance/
│  └─ ui/
│     ├─ search/
│     ├─ settings/
│     ├─ ai/
│     ├─ wallpaper/
│     ├─ widgets/
│     ├─ automation/
│     └─ performance/
└─ include/turingdesk/
```

Canonical implementation roots are explicit, reviewable paths:

```text
src/native/src/app
src/native/src/ai/pi
src/native/src/ai/tools
src/native/src/ai/agent
src/native/src/search
src/native/src/harness
src/native/src/desktop/control
src/native/src/desktop/shell
src/native/src/desktop/wallpaper
src/native/src/desktop/widgets
src/native/src/desktop/automation
src/native/src/desktop/performance
src/native/src/ui/search
src/native/src/ui/settings
src/native/src/ui/ai
src/native/src/ui/wallpaper
src/native/src/ui/widgets
src/native/src/ui/automation
src/native/src/ui/performance
```

## Ownership rules

- `app/` owns executable composition and application startup only.
- `ai/pi/` owns Pi runtime integration; `ai/tools/` owns product/native tool adapters; `ai/agent/` owns Direct fallback agent implementation.
- `desktop/control/` owns the shared Desktop Control facade.
- `desktop/shell/` is the only Windows desktop attachment owner: Progman, WorkerW, raised desktop, Explorer recovery, surface parent/z-order.
- `desktop/wallpaper/` owns wallpaper state and runtime. Renderer-specific code lives below `render/`, `web/`, `monitor/`, `library/` or `runtime/`.
- `desktop/wallpaper/legacy/` is migration-only. No new product behavior may be added there; files leave this directory by deletion/refactor, not by expansion.
- `desktop/widgets/`, `desktop/automation/` and `desktop/performance/` own their domain services/runtime state.
- `ui/wallpaper/`, `ui/widgets/`, `ui/automation/`, `ui/performance/`, `ui/ai/`, `ui/search/` and `ui/settings/` own presentation/intent translation only. UI code must call adapters/controllers/services rather than persistence or WorkerW directly.
- `harness/` owns the Advanced Workbench process/runtime bridge.
- `search/` owns native search/index integration shared by executables.

## Public headers

Public headers intentionally remain under `include/turingdesk/` during this migration so existing include statements and API contracts stay stable while implementation files move. Header namespace/folder subdivision may happen later together with explicit library targets; it must not be mixed into a pure source-layout migration.

## Build graph rule

`src/native/CMakeLists.txt` must mirror the physical tree. Visual Studio source groups also use the physical tree. It is forbidden to reintroduce product implementation files directly under `src/native/src/`.

A build/contract guard (`scripts/verify-native-source-layout.ps1`) enforces the required module directories and rejects flat root-level `.cpp` files.

## Migration rule

Moving a file is not permission to change behavior. Source-layout commits should be mechanically reviewable: same blob where possible, CMake/script/documentation path updates, then exact-head Windows CI. Business refactors continue only after the layout wave is green.
