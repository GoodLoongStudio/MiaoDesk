# Legacy wallpaper Content migration

## Goal

Finish Phase 3 of `CONTENT_PACKAGE_LIFECYCLE.md`: migrate legacy Scene wallpaper library records that still persist an absolute path inside a managed `.mdwall` package to the stable `content:<manifest.id>` identity used by the current runtime.

## Current gap

`WallpaperLibrary::DiscoverPackages` already discovers canonical Scene packages by manifest id and follows a package when its managed directory is renamed. However, an older `library.ini` record can still look like this:

```ini
[Item.legacy-id]
Kind=scene
Source=C:\...\WallpaperLibrary\Packages\foo.mdwall\scene.json
```

The current discovery path compares the canonical package root with the persisted source using exact-path matching. A legacy source that points at `scene.json` therefore does not match the package root and can survive as a second logical record beside `content:<manifest.id>`.

## Migration contract

During `WallpaperLibrary::Load`, before normal package discovery:

1. Consider only `Kind=scene` records that are not already `content:*`.
2. Walk upward from `Source` only while it remains inside the managed wallpaper package root.
3. Stop at the nearest `.mdwall` ancestor.
4. Load the package through `MiaoContentPackage` and require `kind=wallpaper`, `runtime=scene` and a valid Scene payload.
5. Derive `content:<manifest.id>` with `MiaoContentPackageManager::MakeSource`.
6. Preserve user metadata from the legacy record: favorite, import time and last-used time.
7. Replace the legacy INI section only after the stable record is successfully persisted.
8. If a stable record already exists, merge user metadata conservatively instead of creating a duplicate.
9. Do not migrate external `.mdwall` paths. External sources must first go through managed install so runtime resolution does not depend on removable/download locations.
10. Leave malformed or unavailable legacy records unchanged; migration must not make startup fail just because an old package disappeared.

## Duplicate merge policy

When both a legacy record and `content:<id>` exist:

- `favorite`: logical OR
- `imported`: earliest non-zero timestamp
- `lastUsed`: latest timestamp
- title/source/thumbnail: canonical discovered package wins
- delete the legacy INI section only after the canonical record is durable

## Self-test coverage

Extend `WallpaperLibrary::SelfTest` with a canonical Scene package and an injected legacy INI record whose `Source` points to `<package>/scene.json`.

The reload must prove:

- `legacy-id` no longer exists;
- exactly one `content:com.goodloong.selftest.scene` record exists;
- the canonical record resolves to the managed package root;
- favorite and usage timestamps survive migration;
- renaming the managed package after migration still keeps one logical record;
- malformed/external legacy paths are left untouched.

## Non-goals

This migration does not import arbitrary external packages, convert image/video wallpaper records, merge package signatures, or alter monitor assignment semantics. It only normalizes legacy managed Scene wallpaper identity.
