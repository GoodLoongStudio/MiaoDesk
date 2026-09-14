# Canonical wallpaper Library UI filter gate

This gate covers the remaining split between the main Wallpaper Library UI and the
runtime compatibility path for historical built-in assignments.

## Problem

`ContentPackageManagerDialog` already exposes wallpaper packages only when their
identity is canonical `content:<manifest.id>`. The main `WallpaperLibraryWindowV2`
grid currently consumes `WallpaperLibrary::Search()` directly, so an exact shipped
legacy `scene-*` row can still become visible in unusual historical/corrupt Library
states even though it is compatibility state rather than a managed theme identity.

The UI rule and runtime compatibility rule must move together:

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
(`scene-aurora`, `scene-neon`, `scene-grid`) are compatibility-only and must not
appear as a second theme card once the corresponding canonical `.mdwall` package is
available/validated.

The filter must use the narrow shipped-legacy helper (`FindLegacyBuiltinWallpaper`)
and the explicit canonical mapping (`CanonicalBuiltinWallpaperSource`). It must not
hide arbitrary Scene wallpapers, runtime keys, preview keys, user-imported scenes,
or unknown ids merely because they resemble a built-in alias.

Selection restoration must also operate on the filtered canonical identity set so a
previous legacy selection cannot resurrect a hidden compatibility card after search
or refresh.

## Required paired Independent-layout regression

The implementation is not complete unless the Independent layout self-test remains
paired with the UI gate and proves all of the following:

1. A persisted exact shipped `scene-*` assignment resolves successfully when the
   visible Library contains only the canonical `content:<id>` row.
2. A stale Library snapshot containing neither row can still resolve the exact
   shipped `scene-*` assignment through canonical package lookup.
3. The resolved lookup never writes the canonical identity back to
   `monitor-assignments.ini`; reloading assignments from disk must still return the
   original `scene-*` value.
4. A broad alias such as a runtime key is not promoted to canonical package lookup
   by the stale-assignment bridge and falls back safely instead.
5. The Library/UI filtering step itself does not mutate assignments.

## Canonical built-in pairs

- `scene-aurora` -> `content:com.goodloong.miaodesk.theme.miao-cloud`
- `scene-neon` -> `content:com.goodloong.miaodesk.theme.neon-city`
- `scene-grid` -> `content:com.goodloong.miaodesk.theme.mystic-moon`

## ARM64/manual gate

On ARM64 Windows using Chinese, English, and German/Western locale/code-page
configurations:

- the main Library must show one canonical card per built-in `.mdwall` theme and no
  duplicate shipped `scene-*` card;
- theme name, author, title, path, display name, and rule/automation/weather text
  must remain Unicode-correct with no `????` or mojibake;
- existing `scene-*` monitor assignments must survive Library refresh, search,
  package management, apply, restart, and stale-snapshot runtime resolution without
  being rewritten;
- MiaoCloud, NeonCity, and MysticMoon must keep their existing layered `scene.ini`
  visual result while canonical package identity is exposed to UI.

Persisted `scene-*` -> `content:<id>` migration remains intentionally disabled until
a separate transactional migration/rollback gate is implemented and validated.