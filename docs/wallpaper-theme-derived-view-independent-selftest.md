# Wallpaper Theme Derived-View Independent Self-Test

This is an independent acceptance gate for Draft PR #58. It is intentionally separate from monitor-assignment loading so a legacy assignment can remain byte-for-byte unchanged while every user-facing Library view stays canonical-only.

## Required setup

Create a temporary UTF-16LE `library.ini` containing:

- canonical scene item: `content:com.goodloong.miaodesk.theme.neon-city`
- stale shipped legacy item: `scene-neon`
- custom item: `scene-user-custom`
- Chinese title text and a filesystem path containing Chinese characters

Create a separate UTF-16LE monitor-assignment fixture with the exact persisted value `scene-neon`. Capture its bytes before the test.

## Assertions

1. `WallpaperLibrary::Find(L"scene-neon")` returns the canonical item after exact lookup misses.
2. The returned item id is `content:com.goodloong.miaodesk.theme.neon-city`.
3. `Search(L"")`, `Favorites()`, and `RecentlyUsed()` contain no shipped legacy `scene-*` item.
4. `scene-user-custom` remains visible in `Search(L"")`, `Favorites()`, and `RecentlyUsed()` when its metadata marks it favorite/recent.
5. Canonical title, author-derived title, Chinese text, and Chinese filesystem paths round-trip exactly; no `????`, replacement glyphs, or mojibake are allowed.
6. Reading the legacy assignment through the runtime resolver does not write the assignment fixture.
7. After `Find`, `Search`, `Favorites`, `RecentlyUsed`, package-manager browse, apply simulation, and reload, the assignment fixture is byte-identical to the captured pre-test bytes.
8. A missing or malformed canonical `.mdwall` package causes the alias lookup to fail closed; it must not recreate a visible legacy managed row or select a different theme.
9. Reload is idempotent: a second load produces the same canonical visible set and preserves the same assignment bytes.

## Negative cases

The self-test must explicitly prove that the canonical-only filter does not hide:

- `scene-user-custom`
- external scene entries
- unknown ids
- non-shipped user data

It must also prove that a shipped legacy row is not collapsed when its canonical package is absent or invalid.

## Secret handling

Do not log, serialize, screenshot, or include any Desktop AI API key or other secret while executing this gate.

## ARM64 sign-off

Repeat the same test on ARM64 Windows under Chinese, English, and German locale/code-page configurations. Record the exact PR #58 head SHA and retain the original assignment bytes for the no-rewrite assertion.
