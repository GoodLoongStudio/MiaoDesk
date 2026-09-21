# Wallpaper theme alias resolution: Independent layout self-test gate

This gate is intentionally paired with the canonical-only Library UI gate. It verifies the runtime boundary without changing persisted monitor assignments.

## Required fixtures

- Canonical package roots for:
  - `content:com.goodloong.miaodesk.theme.miao-cloud`
  - `content:com.goodloong.miaodesk.theme.neon-city`
  - `content:com.goodloong.miaodesk.theme.mystic-moon`
- Legacy persisted assignments:
  - `scene-aurora`
  - `scene-neon`
  - `scene-grid`
- A stale/in-memory Library snapshot that contains only canonical package rows.
- A negative-control user scene such as `scene-user-custom`.

## Assertions

1. `WallpaperLibrary::Find(L"scene-aurora")`, `Find(L"scene-neon")`, and `Find(L"scene-grid")` perform an exact lookup first and then resolve only the explicit shipped aliases to the corresponding canonical Library item.
2. The resolved descriptor exposes the canonical package source/identity to the runtime renderer, while retaining the original legacy assignment identity in the assignment state.
3. The lookup must not synthesize a fallback loose-scene descriptor when a valid canonical `.mdwall` package is present.
4. Missing or invalid canonical package data must fail closed. It must not resurrect a legacy UI row, invent a new assignment, or rewrite `monitor-assignments.ini`.
5. `scene-user-custom` remains an ordinary user scene and is not treated as a shipped legacy alias.
6. Library `Search()`, `Favorites()`, `RecentlyUsed()`, and package-management enumeration expose only canonical managed wallpaper identities. Compatibility-only `scene-*` rows must not reappear through derived views.
7. Running the self-test twice is idempotent: Library metadata and monitor-assignment bytes are unchanged after the second run.

## Persistence checksum

The test must snapshot the UTF-16LE `monitor-assignments.ini` bytes before and after:

- Library load
- Library refresh/search
- Independent layout resolution
- Apply/restart simulation

The byte sequence must remain identical for all three legacy assignments. This is the acceptance criterion that alias resolution is runtime-only and does not perform a silent migration.

## Independent-layout coverage

The Independent layout test must exercise both paths:

- **Exact-hit path:** the legacy row still exists in the in-memory Library; exact lookup returns it and the canonical package identity is selected for rendering.
- **Exact-miss bridge path:** the legacy row is absent from the in-memory Library; the resolver uses the explicit shipped alias table to locate the canonical package and returns a resolved descriptor carrying the original legacy assignment id.

The second path is the regression guard against fixing only assignment reads while accidentally falling back to the old loose-scene renderer.

## ARM64/manual gate

On ARM64 Windows with a Western locale/code page, repeat the same fixture matrix and verify:

- Chinese/German theme titles, authors, paths, monitor names, and rule names render without `????` or mojibake.
- The rendered visuals for MiaoCloud, NeonCity, and MysticMoon remain unchanged.
- Package-manager and Library UI show canonical `.mdwall` identities only.
- Legacy `scene-*` assignment values remain byte-for-byte unchanged after refresh, apply, restart, install, uninstall, and package validation.
