# Product configuration

This directory contains read-only defaults shipped in `Config/` beside the
three production executables.

- `product.ini` contains release/profile defaults with compiled fallbacks.
- Wallpaper content remains self-describing under `assets/wallpapers/*.mdwall`.
- Mutable user state is not stored here. It remains under
  `%LOCALAPPDATA%\MiaoDesk`.
- Product API keys remain in Windows Credential Manager and are never written
  into shipped configuration, source-controlled local overrides, prompts or logs.

Developer-only private configuration belongs under repository-local ignored
paths such as `.local/` or uses a `*.local.*` / `*.private.*` suffix. See
`docs/LOCAL_PRIVATE_DATA.md` for the repository policy and pre-commit check.

Configuration must be optional at runtime. A missing or invalid shipped file
must fall back to safe compiled defaults so moved or partially repaired installs
remain diagnosable.
