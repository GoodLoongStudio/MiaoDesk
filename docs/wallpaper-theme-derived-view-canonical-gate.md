# Wallpaper Theme Derived-View Canonical Gate

This gate is intentionally independent from monitor-assignment loading. It protects every user-facing wallpaper-library view while keeping persisted legacy identities compatible.

## Invariants

- `WallpaperLibrary::Search`, `Favorites`, `RecentlyUsed`, package-manager browse, and selection restoration expose managed built-in wallpaper themes only as canonical `content:<manifest.id>` identities.
- The shipped compatibility identities remain exact and narrow: `scene-aurora`, `scene-neon`, and `scene-grid`.
- `scene-user-custom`, external scene entries, unknown ids, and non-shipped user data are not filtered by the built-in gate.
- `WallpaperLibrary::Find` performs exact lookup first and only then resolves a known shipped legacy alias to its canonical Library item.
- Alias resolution never writes `monitor-assignments.ini`, never changes the persisted assignment value, and never creates a fallback loose-scene row.
- A missing or invalid canonical `.mdwall` package fails closed: it must not resurrect a legacy UI card and must not silently select a different theme.

## Independent self-test matrix

The Independent layout/self-test must materialize the following state in a UTF-16LE `library.ini` and a separate monitor-assignment fixture:

1. canonical `content:com.goodloong.miaodesk.theme.neon-city` package exists and validates;
2. persisted monitor assignment is still `scene-neon`;
3. the Library contains the canonical item plus a stale legacy row;
4. Favorites and Recently Used both include the stale legacy row before refresh;
5. a refresh/reload runs canonical package discovery and alias resolution.

The test passes only when all of the following are true:

- `Find(L"scene-neon")` returns the canonical item;
- `Search(L"")`, `Favorites()`, and `RecentlyUsed()` contain no shipped legacy `scene-*` item;
- `Search(L"霓虹之城")` returns exactly one canonical item;
- `Find(L"scene-user-custom")` and the corresponding UI view still work;
- the monitor-assignment fixture is byte-for-byte unchanged before and after refresh, apply simulation, and restart simulation;
- deleting or corrupting the canonical package makes the managed theme unavailable instead of reviving the legacy row;
- repeating the reload is idempotent and does not add duplicate canonical entries.

## Unicode gate

The fixture must use Chinese and German user-visible values for title, author, source path, monitor friendly name, and rule/display metadata. The self-test must verify that the UTF-16LE bytes round-trip without literal `????` on a Western Windows locale. No API key or secret is permitted in fixtures, logs, or assertions.

## Implementation rule

Keep the UI visibility predicate shared by all derived views. Do not solve this gate only in assignment read code: a runtime alias lookup without the derived-view checks is a regression because the UI can still expose duplicate legacy identities or silently fall back to a loose scene.
