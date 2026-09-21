# Wallpaper theme UI canonical-identity gate

This gate keeps the wallpaper user interface on the canonical `.mdwall` theme-package model while legacy monitor assignments remain compatible.

## User-visible model

For managed wallpaper packages, UI surfaces must expose exactly one logical identity: `content:<manifest.id>`.

- Install wording: `安装壁纸主题包`.
- Management wording: `壁纸主题包`.
- Package details are derived from the validated package manifest: name, author, version, kind, runtime and canonical content source.
- `scene.ini` and legacy `scene-*` identifiers are renderer/upgrade compatibility details and must not be presented as a second installable or manageable object.
- Reinstall/replace keeps the same canonical content identity when `manifest.id` is unchanged.

## Install/list guard

A wallpaper package shown by package-management UI must already have passed normal `MiaoContentPackage` validation and must expose a canonical source generated from its manifest id.

```text
source == content:<manifest.id>
kind   == wallpaper
runtime == scene | web
```

A legacy loose scene identity, path-derived identity, empty identity, or malformed source must not be introduced into the wallpaper-theme package manager as a new visible package row.

This guard is about new UI/package-management state only. It does not authorize rewriting existing monitor assignments.

## Legacy monitor assignment compatibility

Persisted values such as `scene-aurora`, `scene-neon` and `scene-grid` remain byte-for-byte unchanged during this phase.

Runtime resolution must keep the following order:

1. exact `WallpaperLibrary` lookup;
2. on exact miss only, resolve a known shipped legacy alias through `CanonicalBuiltinWallpaperSource()`;
3. look up the canonical `content:<id>` Library item;
4. if that still misses, retain the existing Independent-layout fallback behavior.

The alias path must never call an assignment write API.

## Upgrade cleanup

Historical source-empty built-in Library rows may be collapsed only after the corresponding canonical package has been discovered and validated. Cleanup must:

- accept only exact shipped legacy ids through `FindLegacyBuiltinWallpaper()`;
- preserve favorite with logical OR;
- preserve the earliest non-zero import time;
- preserve the latest last-used time;
- retain package-derived canonical title/source/thumbnail;
- leave unknown legacy rows and rows whose canonical package is missing or invalid untouched;
- remain idempotent across repeated `WallpaperLibrary::Load()` calls;
- never modify `monitor-assignments.ini`.

## Regression gate

Windows x64 self-tests/CI should prove the paired behavior, not just one side of it:

- Library/UI state contains only the canonical built-in theme row after guarded collapse;
- `Find(L"scene-neon")` still resolves the canonical Neon City package;
- Independent layout with a persisted `scene-neon` assignment resolves that canonical package without fallback;
- the persisted assignment remains `scene-neon` after resolution;
- missing or invalid canonical packages do not cause destructive legacy cleanup;
- Chinese package titles and user metadata survive the UTF-16LE profile round trip without `????`.

## Manual ARM64 gate

Before any persisted identity migration is enabled, validate on ARM64 Windows under at least one Western locale/code page:

- Chinese theme names/authors and package paths display correctly;
- package manager shows only canonical theme-package identities;
- existing `scene-*` assignments continue to render the expected MiaoCloud / NeonCity / MysticMoon layered visuals;
- no assignment file is silently rewritten while browsing, applying, refreshing or restarting;
- install/manage/apply/reinstall/uninstall wording consistently uses the theme-package model.
