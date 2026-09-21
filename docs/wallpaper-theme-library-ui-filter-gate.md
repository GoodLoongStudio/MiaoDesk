# Canonical wallpaper Library UI filter gate

This gate defines the implemented split between the main Wallpaper Library UI and
the runtime compatibility path for historical built-in assignments.

## Implemented boundary

`ContentPackageManagerDialog` exposes wallpaper packages only when their identity is
canonical `content:<manifest.id>`. The main `WallpaperLibraryWindowV2` grid consumes
`WallpaperLibrary::Search()`, and `Search()` now treats the three exact shipped
legacy `scene-*` identities as compatibility-only state rather than visible Library
cards.

The UI rule and runtime compatibility rule move together:

- UI/package-management surfaces expose the built-in `.mdwall` themes only under
  canonical `content:<manifest.id>` identity.
- Existing persisted monitor assignments keep their exact `scene-*` value.
- Runtime resolution may map an exact shipped `scene-*` assignment to its canonical
  package only for lookup when the Library exact lookup cannot provide a usable
  canonical item.
- Runtime keys, preview keys, and other broad catalog aliases must not become a
  persisted-assignment migration bridge.
- No UI refresh, search, package browse, apply, restart, or runtime resolution may
  rewrite `monitor-assignments.ini` from `scene-*` to `content:<id>`.

## Main Library filtering contract

When building the visible wallpaper grid, exact shipped legacy built-in rows
(`scene-aurora`, `scene-neon`, `scene-grid`) are compatibility-only and must never be
presented as managed theme cards. If the corresponding canonical package is absent
or invalid, the UI should show no managed built-in theme card for that package rather
than exposing the legacy row as a substitute identity. Runtime compatibility remains
separate and may still resolve an existing legacy monitor assignment safely.

The filter uses the narrow shipped-legacy helper (`FindLegacyBuiltinWallpaper`). It
must not hide arbitrary Scene wallpapers, runtime keys, preview keys, user-imported
scenes, or unknown ids merely because they resemble a built-in alias. Canonical
package lookup continues to use the explicit mapping
`CanonicalBuiltinWallpaperSource` where runtime resolution requires it.

Selection restoration must operate on the filtered canonical identity set. A
previous `scene-*` selection that is no longer visible is cleared, and the UI may
select a canonical visible item instead; this UI-only selection change must never
write monitor assignment persistence.

## Required paired Independent-layout regression

The implementation is not complete unless the Independent layout self-test remains
paired with the UI gate and proves all of the following:

1. A persisted exact shipped `scene-*` assignment resolves successfully when the
   Library contains only the canonical `content:<id>` row.
2. A stale Library snapshot containing neither row can still resolve the exact
   shipped `scene-*` assignment through canonical package lookup.
3. The resolved lookup never writes the canonical identity back to
   `monitor-assignments.ini`; reloading assignments from disk must still return the
   original `scene-*` value.
4. A broad alias such as a runtime key is not promoted to canonical package lookup
   by the stale-assignment bridge and falls back safely instead.
5. Library search/filtering hides exact shipped legacy rows while preserving unknown
   or user-created Scene rows.
6. Search, refresh, and selection restoration do not mutate the assignment file.

## Canonical built-in pairs

- `scene-aurora` -> `content:com.goodloong.miaodesk.theme.miao-cloud`
- `scene-neon` -> `content:com.goodloong.miaodesk.theme.neon-city`
- `scene-grid` -> `content:com.goodloong.miaodesk.theme.mystic-moon`

## ARM64/manual gate

On ARM64 Windows using Chinese, English, and German/Western locale/code-page
configurations:

- the main Library must show one canonical card per installed/valid built-in
  `.mdwall` theme and no shipped `scene-*` card;
- if a canonical package is missing or corrupt, no legacy compatibility card may
  reappear in UI merely to fill the gap;
- theme name, author, title, path, display name, and rule/automation/weather text
  must remain Unicode-correct with no `????` or mojibake;
- existing `scene-*` monitor assignments must survive Library refresh, search,
  selection restoration, package management, apply, restart, and stale-snapshot
  runtime resolution without being rewritten;
- MiaoCloud, NeonCity, and MysticMoon must keep their existing layered `scene.ini`
  visual result while canonical package identity is exposed to UI.

Persisted `scene-*` -> `content:<id>` migration remains intentionally disabled until
a separate transactional migration/rollback gate is implemented and validated.
