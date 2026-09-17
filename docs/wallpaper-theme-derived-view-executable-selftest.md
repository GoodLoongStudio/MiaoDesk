# Wallpaper Theme Derived-View Executable Self-Test

This is the executable acceptance contract for the canonical wallpaper identity boundary in PR #58.

## Required setup

- Create a temporary UTF-16LE `library.ini` containing:
  - canonical managed rows for `content:com.goodloong.miaodesk.theme.miao-cloud`, `content:com.goodloong.miaodesk.theme.neon-city`, and `content:com.goodloong.miaodesk.theme.mystic-moon`
  - stale shipped legacy rows `scene-aurora`, `scene-neon`, and `scene-grid`
  - one user-authored row `scene-user-custom`
  - one unknown row with a Unicode title and a Chinese filesystem path
- Create a separate UTF-16LE `monitor-assignments.ini` whose values still contain `scene-aurora`, `scene-neon`, or `scene-grid`.
- Snapshot the assignment file bytes before loading the Library.

## Assertions

1. `WallpaperLibrary::Load()` succeeds without converting either file through the active Windows ANSI code page.
2. `Search(L"")`, `Favorites()`, and `RecentlyUsed()` contain canonical managed rows only; none contains `scene-aurora`, `scene-neon`, or `scene-grid`.
3. `scene-user-custom` and the unknown Unicode row remain visible.
4. `WallpaperLibrary::Find(L"scene-aurora")`, `Find(L"scene-neon")`, and `Find(L"scene-grid")` return the corresponding canonical item after an exact lookup miss.
5. The returned descriptor uses the canonical package source and canonical identity for runtime lookup, but does not mutate the requested persisted assignment key.
6. When a canonical package is missing, invalid, or fails scene deserialization, the alias lookup fails closed; it must not return a legacy UI row, a different wallpaper, or a loose-scene fallback.
7. Run the following operations in order: Library refresh, browse/search, Favorites, Recently Used, package-manager browse, apply simulation, reload, and package validation.
8. Compare the post-operation `monitor-assignments.ini` bytes with the pre-operation snapshot. They must be identical.
9. Reload the Library again and repeat assertions 2–8. The second run must be idempotent and must not create or delete legacy assignment data.
10. Verify a Chinese title, German title, Chinese path, display name, and rule name round-trip through the Profile API without `????` or replacement characters.

## Implementation gate

The test is not satisfied by filtering only `Search()`. The same canonical-only predicate must be applied to every user-facing derived view, including `Favorites()` and `RecentlyUsed()`, while `Find()` remains the compatibility-only alias bridge used by runtime assignment resolution.

The test must not log or persist any API key, token, or other secret.
