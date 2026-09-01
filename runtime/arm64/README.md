# ARM64 RuntimeBundle

`runtime/arm64` contains ARM64 runtime artifacts only:

- `node/` — pinned portable Node.js archive
- `harness/` — pinned DeepSeek Harness production archive
- `pi/` — pinned Pi production archive
- `goz/` — pinned ARM64 goz runtime archive
- `runtime-lock.json` — version/source pins
- `runtime-manifest.json` — resolved artifact hashes

Build-only dependencies do not belong here. The shared minimal WebView2 SDK lives at `third_party/webview2/` and is consumed by CMake for both x64 and ARM64.

ARM64 keeps the existing pinned DSH/Pi archives until its package flow is switched to the same unified Runtime V3 Agent graph used by x64.
