# Native code map

Implementation code is grouped under `src/` by process/domain. Do not add new `.cpp` files directly to `src/native/src/`.

- `src/app` — executable composition/startup
- `src/ai` — Pi, native tool adapters, Direct fallback agent
- `src/search` — native search integrations
- `src/harness` — DeepSeek Harness host/runtime bridge
- `src/desktop/control` — Desktop Control facade
- `src/desktop/shell` — Windows desktop attachment/recovery
- `src/desktop/wallpaper` — wallpaper runtime/library/renderers
- `src/desktop/widgets` — widget runtime/domain
- `src/desktop/automation` — playlists/schedules/rules
- `src/desktop/performance` — performance policy
- `src/ui` — presentation/adapters only

Public headers remain in `include/miaodesk/`.

## Executables

- `MiaoDesk.exe` — user-facing application entry
- `MiaoDeskWallpaper.exe` — isolated desktop/wallpaper process
- `MiaoDeskHarness.exe` — isolated DeepSeek Harness host

Diagnostic acceptance executables are not part of the production build graph. Non-trivial runtime behavior is checked through the production binaries' `--self-test` paths and package smoke checks.

## Build rule

CMake builds product code only. Repository policy checks and packaging verification must not become dependencies of native targets. The production package workflow owns path-budget and Runtime smoke verification.
