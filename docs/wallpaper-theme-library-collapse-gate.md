# Wallpaper theme Library legacy-row collapse gate

This gate covers historical `WallpaperLibrary` rows created before built-in `.mdwall` themes were exposed with canonical `content:<manifest.id>` identities.

## Scope

The cleanup applies only to a legacy built-in Scene row whose id is a known shipped alias (`scene-aurora`, `scene-neon`, `scene-grid`) and whose canonical built-in package is already validated and indexed in the same Library load.

It must not rewrite `monitor-assignments.ini`, application rules, `wallpaper.ini`, or any other persisted reference. Runtime compatibility remains the responsibility of the existing alias resolution (`CanonicalBuiltinWallpaperSource` / `LegacyBuiltinWallpaperId`).

## Safe collapse order

For each source-empty legacy built-in row:

1. Resolve the legacy id through `CanonicalBuiltinWallpaperSource`.
2. Require a non-empty canonical id different from the legacy id.
3. Require an exact canonical Library row. Do not create one merely to make cleanup possible.
4. Require that the canonical row represents a managed Scene package and has a non-empty source inside the managed Packages directory.
5. Re-load and validate the canonical `.mdwall` manifest and scene runtime through the normal Miao Content Package path.
6. Merge only user metadata from the legacy row into the canonical row:
   - `favorite`: logical OR;
   - `importedUnixSeconds`: earliest non-zero value;
   - `lastUsedUnixSeconds`: latest value.
7. Keep canonical package-derived fields (`id`, title/name, source, thumbnail, kind, managed-copy state) from the validated canonical row.
8. Persist the canonical row first and flush the UTF-16LE Profile file.
9. Delete only the obsolete legacy Library section.
10. Leave every persisted monitor assignment byte-for-byte unchanged.

If any validation, write, flush, or delete step fails, keep the legacy row and return an error rather than partially collapsing state.

## Required regression coverage

A self-test must seed a UTF-16LE `library.ini` with both:

- canonical Neon City row: `content:com.goodloong.miaodesk.theme.neon-city`, backed by a valid managed `.mdwall` package;
- source-empty legacy row: `scene-neon`, carrying distinct favorite/imported/last-used metadata and a Chinese legacy title.

After `WallpaperLibrary::Load()` the test must verify:

- `Items()` / `Search()` expose only the canonical Neon City item;
- canonical metadata contains the merged favorite, earliest import timestamp and latest last-used timestamp;
- package-derived canonical title/source are retained rather than overwritten by the legacy title;
- `Find(L"scene-neon")` still resolves the canonical item through alias fallback;
- reloading the Library again is idempotent and does not recreate the legacy row;
- a source-empty unknown `scene-*` row is not deleted;
- a known legacy row is not deleted when the canonical package is missing or invalid;
- Chinese metadata survives save/reload exactly and never becomes `????`.

The Independent-layout regression remains separate and must continue proving that a persisted `scene-neon` monitor assignment resolves the canonical package without rewriting the assignment or taking fallback.

## ARM64/manual gate

On ARM64 Windows with English or German system locale/code page, verify an upgraded profile containing historical source-empty built-in Library rows collapses to one visible canonical theme entry, keeps favorite/recent ordering, still applies existing `scene-*` monitor assignments, and preserves the layered MiaoCloud / NeonCity / MysticMoon visuals.
