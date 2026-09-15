# Legacy assignment non-rewrite regression gate

This gate is required for PR #58 before the canonical wallpaper identity work is considered complete.

## Required behavior

- `monitor-assignments.ini` is authoritative persisted user state and must remain byte-for-byte unchanged by Library load, refresh, browse, Favorites, RecentlyUsed, package-manager enumeration, apply, restart recovery, install, uninstall, and package validation.
- A persisted shipped legacy id (`scene-aurora`, `scene-neon`, or `scene-grid`) is a compatibility lookup key only. Runtime resolution may map it to the matching canonical `content:<manifest.id>` package, but the resolved descriptor must retain the original persisted id for diagnostics and persistence boundaries.
- Canonical UI surfaces expose only the `.mdwall` package identity. Legacy `scene-*` rows must never be emitted by Search, Favorites, RecentlyUsed, Package Manager browse, or selection restoration.
- Alias resolution must be exact and allowlisted. Unknown ids, `scene-user-custom`, loose scene ids, and unrelated content ids must not be redirected.
- If the canonical package is absent, malformed, or fails runtime validation, resolution must fail closed. It must not fall back to a loose legacy scene or silently select another package.

## Independent self-test matrix

1. Seed UTF-16LE `monitor-assignments.ini` with each shipped legacy id and at least one Chinese/German monitor name.
2. Load a Library containing only the canonical `.mdwall` package.
3. Verify an exact Library miss for the legacy id resolves to the canonical package.
4. Verify Search, Favorites, RecentlyUsed, Package Manager browse, and selection restoration expose only the canonical id.
5. Run refresh, apply simulation, restart simulation, install, uninstall, and package validation.
6. Compare the assignment file bytes before and after every operation; they must be identical.
7. Repeat with a missing/corrupt canonical package and verify fail-closed behavior plus unchanged assignment bytes.
8. Repeat with `scene-user-custom` and an unknown id; verify no aliasing and no accidental cleanup.

The test must not log or persist API keys or other secrets.
