# MiaoDesk Windows Path Layout Contract

This document is a production contract for Windows 10/11 default environments. MiaoDesk must not require users to enable Win32 long paths, edit the registry, run `subst`, create symlinks, or manually create junctions.

## 1. Build roots are disposable and short

Source location is user-controlled and may be deep. CMake build output must therefore live outside the source tree under a short disposable root.

Preferred CI/developer roots:

- x64 build: `C:\b\MiaoDesk\x64`
- ARM64 build: `C:\b\MiaoDesk\arm64`
- install staging: `C:\pkg\MiaoDesk\<arch>`

`CMAKE_WIN32_LONG_PATHS` or any equivalent long-path helper may be used only as a developer convenience. It is never an end-user requirement or packaging acceptance criterion.

## 2. `cmake --install` is the canonical product tree

Native binaries and first-party assets enter packages only through `cmake --install`.

Canonical root layout:

```text
MiaoDesk\
  MiaoDesk.exe
  MiaoDeskWallpaper.exe
  MiaoDeskHarness.exe
  Assets\
  Wallpapers\
  Runtime\Node\
  Pi\
  Goz\
```

First-party/CMake-owned content should remain at six directory levels or fewer. Repository source trees, SDK trees, package-manager caches, vcpkg `buildtrees`, compiler intermediates, and downloaded archives must never enter the user package.

Third-party runtimes may retain the minimum module-relative structure required for correct resolution, but every shipped file must still satisfy the projected stock-Windows path budget below.

## 3. Runtime payloads are normalized, not blindly copied

The packaged runtime is a production RuntimeBundle, not a developer dependency tree.

For Node-based payloads:

- keep only production runtime files;
- deduplicate identical nested packages;
- safely hoist nested dependencies when Node ancestor resolution preserves semantics;
- never overwrite a shallow package with a different version just to shorten a path;
- after normalization, re-run Pi and DSH entrypoint probes;
- the final path-budget gate is authoritative.

`Runtime\Node`, `Pi`, and `Goz` are stable product-level roots. Do not add extra wrappers such as `third_party\runtime\payload\release\...` around them.

## 4. No runtime dependency on current working directory

All shipped-file discovery must begin from the executable/module directory (for example `GetModuleFileNameW`) or from an explicit path passed by the owning process.

Forbidden production assumptions include:

- locating Runtime/Assets/Wallpapers relative to process CWD;
- requiring the launcher to `cd` to a specific directory for correctness;
- hard-coding a developer checkout path;
- hard-coding `C:\MD`, `C:\MiaoDesk`, or `C:\Program Files\MiaoDesk` as the only valid install root.

A launcher may set CWD for convenience, but moving the complete install tree to another directory must not break runtime discovery.

## 5. Mutable state is separate from the install tree

Program files are relocatable and read-mostly. Mutable user state belongs under `%LOCALAPPDATA%\MiaoDesk\...` (or another Windows known folder explicitly owned by the feature). API keys/secrets belong in Windows Credential Manager.

Do not generate caches, DSH homes, logs, or user data inside nested third-party directories under the installation root.

## 6. Installer policy

NSIS consumes the canonical install staging tree directly and must not add an extra payload wrapper directory.

Default install roots should be shallow and conventional. If the selected install-root string exceeds 85 characters, warn the user but do not block installation.

An optional same-volume junction may be offered only as a compatibility enhancement for a legacy component. The application must remain functional if junction creation fails. Uninstalling a junction must remove the link itself only; never recursively delete through the junction.

No installer path may rely on Windows `LongPathsEnabled`.

## 7. Acceptance budget

CI projects every shipped relative path onto an 85-character install root and requires:

`85 + 1 + relative_path_length <= 248`

The 248-character ceiling intentionally leaves safety margin below traditional `MAX_PATH` for API-specific suffixes and temporary operations.

First-party/CMake-owned content also has a maximum nesting budget of six levels. Third-party Node module depth is not judged by raw level count; it is judged by the stricter projected-path budget plus runtime probes.

## 8. Required release gates

An x64/ARM64 package is not complete until all applicable checks pass:

1. external short-path CMake configure/build;
2. `cmake --install` shallow staging;
3. production RuntimeBundle materialization and normalization;
4. projected path-budget verification without long-path policy;
5. native self-tests plus Node/Pi/DSH runtime probes;
6. move the complete installed tree to a different path containing spaces and verify startup;
7. DSH web smoke startup from the moved tree;
8. only then upload/package/sign/install.

These rules apply to future runtime upgrades as well as the current package. A dependency update that violates the contract must be normalized, pruned, replaced, or repackaged before release; the path budget must not simply be raised to hide the regression.
