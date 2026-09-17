# ARM64 Unicode + Canonical Wallpaper Theme Validation

This checklist is the manual release gate for Draft PR #58. It does not enable persisted identity migration and must not rewrite legacy monitor assignments.

## Test environment

Run on a real Windows on ARM64 device. At minimum repeat the persistence checks under:

- Chinese Windows locale/code page
- English Windows locale/code page
- German Windows locale/code page

Use a clean profile once, then repeat with an upgraded profile containing legacy `scene-*` assignments and existing Library metadata.

## Hard Unicode acceptance

Every user-visible value below must survive save, app restart, Windows sign-out/restart where applicable, and reload with the exact original Unicode text. Literal `????`, replacement glyphs caused by persistence, mojibake, or ANSI-code-page loss is a failure.

Exercise Chinese and mixed Unicode values in:

- wallpaper Library titles, package titles, authors, source paths, and thumbnail paths
- wallpaper file/package paths containing Chinese characters
- monitor friendly names persisted with monitor assignments
- application rule display names
- automation profile, playlist, and schedule names
- native weather location, condition, status, and hourly labels
- global wallpaper source paths in `wallpaper.ini`
- legacy WallpaperEngineProduction Profile API compatibility paths while that bridge remains enabled

For each relevant INI/Profile file, verify the persisted file remains UTF-16LE-compatible and reopening the application reproduces the original text exactly.

Do not log, dump, screenshot, or otherwise expose Desktop AI API keys while checking DesktopAiSettingsPage persistence.

## Canonical theme UI identity

The three shipped themes must appear to users only as canonical managed `.mdwall` packages:

- `content:com.goodloong.miaodesk.theme.miao-cloud`
- `content:com.goodloong.miaodesk.theme.neon-city`
- `content:com.goodloong.miaodesk.theme.mystic-moon`

Pass criteria:

1. Package manager/library UI shows one managed item per shipped theme, never a second visible `scene-*` copy.
2. Install, reinstall, manage, and immediate-apply flows operate on the canonical `content:<manifest.id>` identity.
3. Package title/author text remains correct Unicode on Chinese, English, and German Windows.
4. A malformed, empty, or legacy loose identity must not surface as a managed wallpaper package.

## Legacy assignment compatibility without rewrite

Prepare persisted monitor assignments using:

- `scene-aurora`
- `scene-neon`
- `scene-grid`

For each assignment, verify all of the following:

1. Library UI contains only the corresponding canonical package item.
2. Independent layout resolves the old assignment to the canonical package without global fallback.
3. The expected theme renders on the assigned monitor.
4. Browsing the Library does not rewrite the assignment.
5. Package-manager refresh does not rewrite the assignment.
6. Applying/reapplying the theme does not silently migrate an existing legacy assignment as a side effect of lookup.
7. Application restart does not rewrite the assignment.
8. Re-read `monitor-assignments.ini` from disk after each operation and confirm the stored value remains the original `scene-*` string.

A runtime alias may resolve to canonical content; persisted migration remains disabled in this PR.

## Historical Library row collapse

Create an upgraded profile with a source-empty shipped legacy Library row and an installed valid canonical package. Include a Chinese legacy title and user metadata.

Pass criteria:

- the visible Library ends with only the canonical package row
- legacy `favorite` is preserved
- earliest `Imported` is preserved
- latest `LastUsed` is preserved
- canonical package title/source/thumbnail remain authoritative and are not overwritten by the legacy row
- the Chinese legacy metadata round-trips without `????` while the row exists
- `monitor-assignments.ini` is byte/identity-equivalent with respect to the wallpaper assignment value
- a second reload is idempotent

Negative cases that must remain untouched:

- unknown source-empty `scene-*`/custom row
- shipped legacy row whose canonical package is absent
- shipped legacy row whose canonical package is invalid/corrupt

## Uninstall/reinstall continuity

With a canonical `content:<id>` assignment, uninstall the package and verify the resolver safely falls back while retaining the stable assignment. Reinstall the same package id and verify the assigned theme resumes without rewriting the assignment.

Repeat the legacy `scene-*` assignment case and verify uninstall/reinstall never converts the persisted legacy id merely because alias resolution occurred.

## Layered visual regression

Validate the production layered `scene.ini` rendering path for:

- MiaoCloud
- NeonCity
- MysticMoon

Compare against the pre-PR visual baseline under Explorer desktop composition. Check layer ordering, motion, scaling/cropping, multi-monitor placement, and restart behavior. The canonical `scene.json`/manifest migration must not change the production visual result in this PR.

## Sign-off record

For every tested device/locale record:

- Windows edition/build
- ARM64 device/model
- locale and system code page configuration
- clean-profile or upgraded-profile case
- exact PR #58 head SHA
- result for Unicode persistence
- result for canonical-only UI identity
- result for legacy assignment no-rewrite
- result for source-empty Library collapse
- result for layered visual regression

Any `????`/mojibake, duplicate visible legacy theme identity, persisted assignment rewrite, alias fallback regression, or layered visual difference is a release blocker for #58.
