# Wallpaper theme identity migration

This document defines the compatibility contract for moving wallpaper themes from legacy `scene-*` library identities to canonical Miao Content Package identities (`content:<manifest.id>`).

## Invariants

The migration must preserve all of the following:

- Existing `scene-aurora`, `scene-neon`, `scene-grid`, and other legacy scene assignments continue to resolve until an explicit migration succeeds.
- Existing layered `scene.ini` rendering remains visually unchanged while built-in themes carry canonical `scene.json` metadata in parallel.
- A canonical package is identified by `manifest.id`, not by directory name, download path, or current package location.
- The same package id must not produce duplicate library items after reinstall, package rename, or package replacement.
- Monitor assignments must never be rewritten to a canonical id unless the canonical package has already been validated and indexed successfully.
- User-visible names, monitor friendly names, rule names, and other profile-backed Unicode text must survive the migration byte-for-byte through the UTF-16LE profile path.
- Migration must not change Store identity/signing and must not require release-time behavior.

## Canonical identity

For a validated wallpaper package:

```text
manifest.id = com.goodloong.wallpaper.miaocloud
library id  = content:com.goodloong.wallpaper.miaocloud
```

`manifest.id` is the stable identity. `name`, `version`, package directory, `entry`, and preview assets may change between versions without changing assignment identity.

The canonical wallpaper manifest contract is:

```text
schema
id
name
author
version
kind = wallpaper
runtime = scene | web
entry = canonical runtime entry
```

For migrated layered scenes, `entry` points at canonical `scene.json`. A compatibility field such as `legacy_entry=scene.ini` may remain while the production renderer still depends on the legacy layered scene path.

## Migration phases

### Phase 0: dual identity, no assignment rewrite

Built-in and managed `.mdwall` packages expose canonical metadata and are indexed by stable content id, while existing `scene-*` assignments remain untouched.

This is the current safe compatibility phase.

### Phase 1: resolve aliases

Add an explicit alias table from known legacy scene ids to canonical content ids. Resolution may use the alias as a fallback, but persistence still keeps the original assignment id.

Example:

```text
scene-aurora -> content:com.goodloong.wallpaper.miaocloud
scene-neon   -> content:com.goodloong.wallpaper.neoncity
scene-grid   -> content:com.goodloong.wallpaper.mysticmoon
```

The concrete mapping must be derived from the shipped manifests and covered by tests rather than inferred from folder names.

### Phase 2: transactional assignment migration

Only after canonical package validation and alias resolution are proven should persisted monitor assignments be rewritten.

For each legacy assignment:

1. Load the current assignment without modifying it.
2. Resolve its legacy id to one exact canonical content id.
3. Resolve and validate the canonical package through `MiaoContentPackageManager`.
4. Verify the package has `kind=wallpaper` and a supported runtime.
5. Verify the concrete runtime entry exists and passes the same validation used by normal package install/indexing.
6. Persist a temporary UTF-16LE assignment file containing the canonical id while preserving monitor id and friendly name exactly.
7. Reload the temporary file and verify the round-trip values.
8. Atomically replace the original assignment file.
9. Request the normal wallpaper runtime reload through the existing service path.

If any step fails, keep the legacy assignment unchanged.

### Phase 3: retire aliases only after compatibility window

Legacy aliases can be removed only after supported upgrades can no longer contain persisted `scene-*` identities. Until then, resolution should remain backward compatible even if new writes use canonical identities.

## Duplicate and reinstall handling

When both a legacy record and canonical record describe the same managed package:

- prefer the canonical `content:<id>` record;
- preserve `favorite` with logical OR;
- preserve the earliest non-zero import timestamp;
- preserve the latest last-used timestamp;
- refresh package name/source/preview from the currently validated canonical package;
- write the canonical record before deleting an obsolete duplicate record.

Reinstalling or replacing a package with the same `manifest.id` must update the existing canonical item instead of creating a new identity.

## Unicode persistence gate

Any migration code that touches Win32 Profile/INI storage must use the shared Unicode profile helper before reads or writes. A BOM-less profile file must never be allowed to make the active Windows ANSI code page determine persistence.

Automated coverage must include at least one round-trip containing Chinese text for each profile-backed user-visible category touched by the migration, including monitor friendly names and theme/library titles.

A useful sentinel is:

```text
中文主题标题 / 作者-妙桌 / 主显示器-中文
```

The test must compare the exact reloaded wide strings and must fail on replacement text such as `????`.

## Automated verification gates

Before enabling persisted identity rewriting, Windows x64 CI should cover:

1. legacy `scene-*` assignment resolves before migration;
2. canonical package is validated and indexed under `content:<id>`;
3. layered legacy visual entry remains available during the compatibility phase;
4. migration writes a canonical assignment through the real assignment store;
5. monitor id and Unicode friendly name survive exact round-trip;
6. reload resolves the canonical assignment successfully;
7. package directory rename/reinstall with the same manifest id does not change assignment identity;
8. missing/invalid canonical package leaves the legacy assignment untouched;
9. duplicate legacy/canonical library records collapse without losing favorite/imported/last-used metadata.

Do not implement these checks by editing INI files behind the service/store APIs when an existing product path exists.

## Manual ARM64 / Windows UI gate

Before the migration can be enabled for users, verify on ARM64 Windows, including a Western locale/code page:

- Chinese theme name and author render without `????`;
- monitor friendly names and rule names render without `????`;
- existing `scene-*` assignments still apply before migration;
- canonical assignments apply after explicit migration;
- MiaoCloud, NeonCity, and MysticMoon layered visuals are unchanged;
- install/manage/apply wording consistently presents `.mdwall` as a wallpaper theme package;
- package reinstall and uninstall behavior remains coherent with the displayed canonical identity.

## Enablement rule

Do not silently rewrite all legacy assignments merely because canonical packages exist. Enable persisted `scene-* -> content:<id>` migration only after the alias mapping, transactional write path, Windows x64 regression coverage, and ARM64 visual/Unicode checks above are all satisfied.
