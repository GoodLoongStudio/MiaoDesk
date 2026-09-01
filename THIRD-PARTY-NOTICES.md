# Third-Party Notices

MiaoDesk includes or redistributes the following third-party components. Runtime versions are pinned by the architecture runtime locks; applicable upstream license files are retained in the vendored payloads or alongside them.

## Pi

- Project: Pi
- Package: `@earendil-works/pi-coding-agent`
- Source: https://github.com/earendil-works/pi
- Pinned MiaoDesk version: `0.83.0`
- License: MIT
- MiaoDesk usage: default Agent Runtime for ordinary AI and desktop-agent requests, launched in RPC mode on MiaoDesk's private Node.js runtime.

## DeepSeek Harness

- Project: DeepSeek Harness
- Package: `@deepseek-ai/dsh`
- Source: https://github.com/deepseek-ai/deepseek-harness
- License: MIT
- MiaoDesk usage: official upstream package launched by `MiaoDeskHarness.exe` and rendered inside MiaoDesk WebView2.

The Windows x64 package resolves Pi and DeepSeek Harness together into one production `Runtime/Agent` dependency graph. End-user machines do not install them with npm or npx.

## Node.js

- Project: Node.js
- Source: https://nodejs.org/ / https://github.com/nodejs/node
- License: Node.js project license plus licenses for bundled third-party components.

Node is private to MiaoDesk and is not installed system-wide.

## goz

- Project: goz
- Source: https://github.com/mustafaahci/goz
- Pinned MiaoDesk version: `v0.1.1`
- License: MIT
- MiaoDesk usage: NTFS filename-search backend through `goz.exe` / `gozd.exe`.

## Microsoft WebView2 SDK

- Project: Microsoft Edge WebView2 SDK
- Package: `Microsoft.Web.WebView2`
- Pinned SDK version: `1.0.4129.50`
- Source: https://www.nuget.org/packages/Microsoft.Web.WebView2
- License/notices: retained under `third_party/webview2/`.

MiaoDesk keeps only the headers and x64/ARM64 static loaders required by the native build, plus the upstream license/notice and `manifest.json`. The Microsoft Edge WebView2 Runtime itself is treated as an operating-system component and is not duplicated in the repository.

## Microsoft PowerToys

- Project: Microsoft PowerToys / PowerToys Run Program plugin
- Source: https://github.com/microsoft/PowerToys
- License: MIT
- Copyright: Copyright (c) Microsoft Corporation. All rights reserved.
- MiaoDesk usage: application discovery follows the mature pattern of combining classic Windows program shortcuts with packaged-app identities/AUMIDs; the implementation is native to MiaoDesk.

## Flow Launcher

- Project: Flow Launcher
- Source: https://github.com/Flow-Launcher/Flow.Launcher
- License: MIT
- Copyright: Copyright (c) 2019 Flow-Launcher; Copyright (c) 2015 Wox
- MiaoDesk usage: the in-memory matcher is an independent compact adaptation of ordered-subsequence matching, contiguous-match bonuses, word-boundary bonuses and early-match weighting.
