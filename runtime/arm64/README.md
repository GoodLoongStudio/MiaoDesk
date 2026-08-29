# MiaoDesk ARM64 RuntimeBundle

This directory is the repository-vendored, network-free runtime used by formal Windows ARM64 builds and local deployment.

The bundle is generated from `runtime-lock.json` by `.github/workflows/vendor-arm64-runtime.yml`. Normal CMake builds and end-user updates must not install third-party runtimes from the network.

Runtime components:

- `node/` — official portable Node.js ARM64 archive.
- `pi/` — pinned production dependency tree of `@earendil-works/pi-coding-agent`; Pi is the default MiaoDesk Agent Runtime.
- `harness/` — pinned production tree of `@deepseek-ai/dsh`; Harness remains the independent advanced workbench.
- `goz/` — Windows ARM64 `goz.exe` + `gozd.exe`; `gozd` is the MFT/USN index service.
- `webview2-sdk/` — pinned WebView2 headers and ARM64 static loader used at build time.

Pi uses the MiaoDesk-bundled Node runtime. On Windows its built-in shell tool is configured to use the system Windows PowerShell executable, so Git Bash is not an end-user prerequisite.

`runtime-manifest.json` contains SHA-256 hashes for every vendored artifact. `scripts/verify-arm64-runtime-bundle.ps1` rejects stale, missing, corrupted, or retired bundles before native compilation.
