# Canonical identity gate for derived wallpaper Library views

This gate applies to every user-facing wallpaper Library projection, not only the primary search grid.

## Required invariant

User-facing wallpaper theme views must expose shipped built-in themes only through canonical managed package identities:

- `content:com.goodloong.miaodesk.theme.miao-cloud`
- `content:com.goodloong.miaodesk.theme.neon-city`
- `content:com.goodloong.miaodesk.theme.mystic-moon`

The exact legacy identities `scene-aurora`, `scene-neon`, and `scene-grid` remain compatibility-only storage/runtime identities. They must not reappear in Favorites, Recently Used, search results, selection restoration, package-manager lists, or any other derived Library view.

## Why this is a separate gate

`WallpaperLibrary::Find()` intentionally performs compatibility lookup for legacy monitor assignments. That behavior must not leak into projections that feed UI lists. A derived view may use the storage snapshot internally, but its returned items must pass the same narrow canonical-only visibility predicate as the primary Library search.

## Self-test matrix

1. Seed canonical package rows and exact shipped legacy rows in the same Library snapshot.
2. Verify `Search(L"")` contains no exact shipped `scene-*` row.
3. Verify `Favorites()` contains no exact shipped `scene-*` row while preserving canonical favorite state.
4. Verify `RecentlyUsed()` contains no exact shipped `scene-*` row while preserving canonical recency ordering.
5. Verify an unknown/custom scene such as `scene-user-custom` remains eligible for user-facing views.
6. Verify `Find(L"scene-neon")` still resolves to the canonical item when the exact legacy row is absent.
7. Verify no derived-view call writes `monitor-assignments.ini` or changes the persisted assignment value.

## Negative cases

The filter must not be broadened to hide:

- user-created `scene-*` ids that are not exact shipped aliases
- runtime keys such as `neon`
- preview keys
- canonical `content:<id>` identities
- unknown or corrupt packages that are not proven shipped built-ins

## Acceptance

A failure in any derived view is a canonical identity regression even when the primary search grid is correct. The result is a Draft PR #58 blocker until x64 CI and ARM64 UI validation both pass.
