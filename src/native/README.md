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

See `docs/NATIVE_SOURCE_LAYOUT.md` and `docs/DESKTOP_DOMAIN_ARCHITECTURE.md` for the normative dependency rules.
