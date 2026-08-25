# Native code map

Implementation code is grouped under `src/` by process/domain. Do not add new `.cpp` files directly to `src/native/src/`.

- `src/app` — executable composition/startup
- `src/ai` — Pi, native tool adapters, Direct fallback agent
- `src/search` — native search integrations
- `src/harness` — Advanced Workbench process/runtime bridge
- `src/desktop/control` — Desktop Control facade
- `src/desktop/shell` — Windows desktop attachment/recovery
- `src/desktop/wallpaper` — wallpaper runtime/library/renderers
- `src/desktop/widgets` — widget domain
- `src/desktop/automation` — playlists/schedules/rules
- `src/desktop/performance` — performance policy
- `src/ui` — presentation/adapters only

Public headers remain in `include/turingdesk/` for API stability during this migration.

## Build organization

`CMakeLists.txt` mirrors the physical tree with separate source ownership sets for:

- `TuringDesk.exe`
- `TuringDeskWallpaper.exe`
- `TuringDeskHarness.exe`

Visual Studio also mirrors the directory hierarchy through `source_group(TREE ...)`, so the IDE view and repository layout no longer diverge.

The next build-graph cleanup is intentionally separate from this physical move: after the source-layout wave is green, stable domains can become explicit CMake library/object targets (`desktop_shell`, `desktop_control`, `widgets`, `automation`, `performance`, etc.) so dependency direction is enforced by the linker/build graph rather than only by source ownership guards.

## Guardrails

`scripts/verify-native-source-layout.ps1` fails the build if root-level implementation `.cpp` files return under `src/native/src/`, required module directories disappear, CMake stops mirroring the module tree, or the normative layout documentation drifts from the repository.

See `docs/NATIVE_SOURCE_LAYOUT.md` and `docs/DESKTOP_DOMAIN_ARCHITECTURE.md` for the normative dependency rules.
