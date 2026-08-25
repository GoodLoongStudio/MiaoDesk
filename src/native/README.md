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

The next build-graph cleanup is intentionally separate from this physical move: stable domains may become explicit CMake library/object targets (`desktop_shell`, `desktop_control`, `widgets`, `automation`, `performance`, etc.) when doing so improves enforceable dependency direction rather than only cosmetics.

## Active Widget runtime boundary

M3 Widget health remains owned by `src/desktop/widgets`. Runtime process/HWND/WebView2/z-order inspection is translated into `WidgetSurfaceHealth` and exposed through `DesktopControlService::GetSnapshot()`. UI and Pi consume the same `issueCode`, `detail` and `recommendedAction`; they must not enumerate HWNDs or read private runtime diagnostics themselves.

The production legacy Widget list is still compatibility UI, but its Widget data and temporary health decoration are supplied by `src/ui/widgets/DesktopWidgetUiAdapter.cpp`. The display copy is deliberately separate from persisted Widget data so runtime warning text cannot leak into stored titles.

## Guardrails

`scripts/verify-native-source-layout.ps1` fails the build if root-level implementation `.cpp` files return under `src/native/src/`, required module directories disappear, CMake stops mirroring the module tree, or the normative layout documentation drifts from the repository.

`scripts/verify-desktop-domain-contract.ps1` additionally guards Desktop Control routing, Widget actionable-health ownership, Pi/UI snapshot consumption and the read-only DesktopSurfaceTelemetry boundary.

See `docs/NATIVE_SOURCE_LAYOUT.md`, `docs/DESKTOP_DOMAIN_ARCHITECTURE.md` and `docs/WIDGET_RUNTIME_HEALTH_M3.md` for the normative dependency/runtime contracts.
