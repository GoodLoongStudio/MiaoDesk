# Product configuration

This directory contains read-only defaults shipped in `Config/` beside the
three production executables.

- `product.ini` contains release/profile defaults with compiled fallbacks.
- Wallpaper content remains self-describing under `assets/wallpapers/*.mdwall`.
- Mutable user state is not stored here. It remains under
  `%LOCALAPPDATA%\MiaoDesk` and secrets remain in Windows Credential Manager.

Configuration must be optional at runtime. A missing or invalid shipped file
must fall back to safe compiled defaults so moved or partially repaired installs
remain diagnosable.
