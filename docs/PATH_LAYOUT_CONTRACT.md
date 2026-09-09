# MiaoDesk Windows Path Layout Contract

This is a production contract for stock Windows 10/11 environments. MiaoDesk must not require users to enable Win32 long paths, edit long-path registry policy, run `subst`, or create symlinks/junctions.

## Build roots

Source location is user-controlled and may be deep. Build and staging output must stay outside the source tree under short disposable roots.

```text
C:\b\MiaoDesk\x64
C:\b\MiaoDesk\arm64
C:\pkg\MiaoDesk\<arch>
```

`CMAKE_WIN32_LONG_PATHS` may be a developer convenience only. It is not a packaging acceptance criterion.

## Canonical repository Runtime

Architecture-independent Agent dependencies live once under `runtime/agent`. Architecture directories contain only native/base Runtime artifacts.

```text
runtime/
  agent/
    package.json
    package-lock.json
  x64/
    node/
    goz/
    runtime-lock.json
  arm64/
    node/
    goz/
    runtime-lock.json
```

The following legacy paths are forbidden under `runtime/<arch>`:

```text
harness/
pi/
.complete
runtime-manifest.json
webview2-sdk/
```

DSH/Pi versions belong only to `runtime/agent/package.json`; their complete transitive graph belongs only to `runtime/agent/package-lock.json`. `runtime/<arch>/runtime-lock.json` owns only Node/Goz archive names, sources and SHA-256 values.

## Canonical product tree

First-party files enter packages through `cmake --install`; runtime materialization extends that same staging tree.

```text
MiaoDesk\
  MiaoDesk.exe
  MiaoDeskWallpaper.exe
  MiaoDeskHarness.exe
  Assets\
  Config\
    product.ini
  Wallpapers\
  Runtime\
    Node\
  AI\
    package.json
    package-lock.json
    node_modules\
  Goz\
```

`AI` is the single production dependency graph for Pi + DeepSeek Harness on both x64 and ARM64. Pi keeps its published package name but uses the shorter physical directory `AI/node_modules/pi` in the shipped tree. Do not restore separate DSH and Pi dependency trees.

Repository source trees, compiler output, SDKs, npm caches and downloaded archives must never enter the user package. Build-only WebView2 lives under `third_party/webview2`, not `runtime/<arch>`.

## Runtime discovery

Shipped-file discovery begins from the executable/module directory (`GetModuleFileNameW`) or an explicit owner-supplied path. Production code must not depend on process CWD or a developer checkout path.

The complete install tree must remain movable after packaging.

## Mutable state

Program files are relocatable/read-mostly. Mutable state belongs under `%LOCALAPPDATA%\MiaoDesk\...`; secrets belong in Windows Credential Manager. Do not write caches, DSH home, logs or user data into the installed third-party dependency tree.

Native code resolves this root through the shared `miaodesk::paths` helpers.
Individual domains must not implement their own `LOCALAPPDATA` fallback logic.

## Installer

NSIS consumes the canonical staging root directly. It must not build a second Runtime tree or add wrapper directories.

Default paths stay shallow. If a selected install root exceeds 85 characters, warn but do not block. The installer must not create path-shortening junctions or modify `LongPathsEnabled`.

Uninstall removes only known MiaoDesk-owned files/subdirectories and must not recursively delete an arbitrary user-selected install root.

## Path budget

CI projects every shipped relative path onto an 85-character install root and requires:

```text
85 + 1 + relative_path_length <= 248
```

First-party content also stays at six directory levels or fewer. Third-party module depth is governed by the stricter projected-path budget plus real runtime probes.

Runtime staging may remove TypeScript declarations and JavaScript source maps,
which Node never loads in the shipped product. It must not delete executable
JavaScript merely to satisfy the budget.

## Release gates

A Windows package is complete only after:

1. external short-path CMake configure/build;
2. `cmake --install` staging;
3. production Runtime materialization from pinned locks;
4. projected path budget `<= 248`;
5. native self-tests;
6. Pi and DSH CLI probes;
7. moving the complete install tree to another path containing spaces;
8. real DSH Web startup from the moved tree;
9. package upload/installer generation.

A dependency upgrade that violates this contract must be repackaged or replaced. Do not raise the path ceiling to hide the regression.
