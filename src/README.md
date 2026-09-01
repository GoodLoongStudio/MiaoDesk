# Source map

MiaoDesk product C++ lives directly under `src/` by process/domain. Do not add unrelated `.cpp` files to the `src/` root.

- `app/` — executable composition/startup
- `ai/` — Pi, native tool adapters, Direct fallback agent
- `search/` — native search integrations
- `harness/` — DeepSeek Harness host/runtime bridge
- `desktop/control/` — Desktop Control facade
- `desktop/shell/` — Windows desktop attachment/recovery
- `desktop/wallpaper/` — wallpaper runtime/library/renderers
- `desktop/widgets/` — widget runtime/domain
- `desktop/automation/` — playlists/schedules/rules
- `desktop/performance/` — performance policy
- `ui/` — presentation/adapters only

Shared headers remain under `include/miaodesk/` so existing `#include <miaodesk/...>` statements stay stable. New target-local headers should live beside their implementation unless they are genuinely shared across domains or processes.

## Executables

- `MiaoDesk.exe` — user-facing application entry
- `MiaoDeskWallpaper.exe` — isolated desktop/wallpaper process
- `MiaoDeskHarness.exe` — isolated DeepSeek Harness host

Diagnostic acceptance executables are not part of the production build graph. Non-trivial runtime behavior is checked through the production binaries' `--self-test` paths and package smoke checks.

## Build rule

CMake builds product code only. Repository policy checks and packaging verification must not become dependencies of native targets. The production package workflow owns path-budget and Runtime smoke verification.
