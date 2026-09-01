# MiaoDesk Repository + Runtime V3 Refactor Plan

Status: normative migration plan
Target branch: `main`
Scope: repository layout first, packaged runtime layout second

## 1. Why this refactor exists

The current MiaoDesk product can build and package successfully, but the repository and runtime delivery model have accumulated multiple responsibilities in the same physical trees:

- build-only SDKs are stored below architecture runtime directories;
- x64 and ARM64 runtime folders separately carry Node, DeepSeek Harness, Pi and WebView2 SDK concerns;
- the package materializer expands one Harness dependency tree and one Pi dependency tree, then repairs the result late with path normalization / hoisting;
- root-level developer CMD entrypoints, build scripts, packaging scripts, verification scripts and runtime scripts are mixed together;
- production executables, internal host executables and diagnostic executables are all created by the same native build graph without a formal product-facing process classification;
- current full x64 staging contains tens of thousands of files, overwhelmingly from two Node dependency trees.

The Runtime V3 migration therefore starts with repository ownership and path cleanup. Package pruning is not allowed to become a substitute for a correct source/build layout.

## 2. Non-negotiable constraints

1. Completed work is delivered on `main`.
2. Windows 10/11 default installations must work without enabling Win32 long paths, registry edits, `subst`, `mklink` or other user actions.
3. Build directories remain outside the repository and use short external paths.
4. The repository must not contain expanded `node_modules` runtime trees.
5. Build SDKs and end-user runtimes are separate concerns and must not share the same ownership directory.
6. x64 and ARM64 use the same logical Runtime V3 layout. Only architecture-dependent binaries/artifacts differ.
7. Production packages are generated from an explicit install/runtime manifest, not by recursively copying arbitrary repository folders.
8. The package path budget remains <= 248 projected characters for an 85-character user installation root.
9. Runtime pruning may remove only explicitly classified non-runtime metadata. It must never be driven solely by "this path is long".
10. Every runtime structural change must pass real Pi CLI, DSH CLI and DSH Web startup tests from a moved installation directory.

## 3. Process / EXE policy

Multiple executables are acceptable when they represent real process boundaries. The problem is not "more than one EXE"; the problem is exposing internal and diagnostic processes as if every EXE were a user-facing product.

MiaoDesk will classify native executables into three groups.

### 3.1 User entry process

Exactly one executable is presented as the product entrypoint:

- `MiaoDesk.exe`

It remains at the installation root and is the executable referenced by Start Menu shortcuts, file associations, installer metadata and normal user documentation.

### 3.2 Internal isolated hosts

Processes that require crash isolation, desktop-shell lifetime isolation, independent restart policy or separate WebView/runtime ownership may remain separate executables, but they are internal implementation details.

Current candidates:

- `MiaoDeskWallpaper.exe`
- `MiaoDeskHarness.exe`

Target policy:

- evaluate each process boundary before changing behavior;
- retain the wallpaper host as a separate process unless measurements prove that merging improves reliability;
- review the Harness process after Runtime V3 is stable. If it is only a supervisor/bridge, merge its supervision into the main process. If it owns an independent WebView/Workbench lifetime, keep an internal Agent/Workbench host;
- internal host executables should ultimately live below an internal directory such as `Runtime/Hosts/`, not beside the public product entrypoint;
- internal hosts are started only by MiaoDesk or its service/runtime supervisor and are not directly advertised to users.

A future naming cleanup may use explicit internal names such as `MiaoDesk.WallpaperHost.exe` and `MiaoDesk.AgentHost.exe`, but renaming is not part of the first physical move because process names are referenced by diagnostics, shutdown logic and update code.

### 3.3 Diagnostic / acceptance executables

Examples:

- `MiaoDeskWidgetAcceptance.exe`

These are build/test artifacts only. They may be uploaded as CI diagnostics but must never be installed in the normal end-user package.

## 4. Repository target layout

The migration preserves stable source-domain ownership while cleaning top-level product/build/runtime concerns.

```text
MiaoDesk/
  CMakeLists.txt
  CMakePresets.json
  README.md
  LICENSE

  src/
    native/
      CMakeLists.txt
      include/miaodesk/
      src/
        app/
        ai/
        desktop/
        harness/
        search/
        ui/

  assets/
    wallpapers/
    app/

  runtime/
    workspace/
      package.json
      package-lock.json
      entries/
        dsh-entry.mjs
        pi-entry.mjs
    manifests/
      runtime-versions.json
      x64.json
      arm64.json
    vendor/
      node/
        x64/
        arm64/
      native/
        goz/
          x64/
          arm64/
    bundles/
      x64/
      arm64/

  third_party/
    webview2/
      <version>/
        include/
        x64/
        arm64/

  packaging/
    nsis/
    msix/
    store/
    assets/

  tools/
    build/
    runtime/
    package/
    verify/
    dev/
      windows/

  docs/
  tests/
  .github/workflows/
```

This is a logical target. Exact moves are staged and references are updated atomically; the repository is never intentionally left with broken paths between commits.

## 5. Repository path problems to fix first

### 5.1 Separate WebView2 SDK from RuntimeBundle

Current architecture runtime trees include `webview2-sdk`, but WebView2 SDK headers/static loader are compile-time dependencies, not end-user runtime content.

Move them to a build-only vendor tree:

```text
third_party/webview2/<version>/include
third_party/webview2/<version>/x64/WebView2LoaderStatic.lib
third_party/webview2/<version>/arm64/WebView2LoaderStatic.lib
```

CMake reads WebView2 only from `third_party/`. Package staging must have no code path that recursively copies this tree.

### 5.2 Consolidate runtime version ownership

Today x64 and ARM64 each own a runtime lock/manifest with largely duplicated logical versions.

Runtime V3 separates:

- logical versions shared across architectures;
- architecture-specific archive/hash metadata.

Example:

```text
runtime/manifests/runtime-versions.json
runtime/manifests/x64.json
runtime/manifests/arm64.json
```

`runtime-versions.json` owns Node / DSH / Pi / Goz logical versions. Architecture manifests own binary archive hashes and platform-specific native assets.

### 5.3 Remove root-level command clutter

Root-level developer commands such as deploy/preview/diagnose/init launchers should be moved under:

```text
tools/dev/windows/
```

If a one-click developer entrypoint is still valuable, keep at most one documented root launcher temporarily as a compatibility shim and delete it after callers/docs/CI are migrated.

### 5.4 Split scripts by responsibility

The current `scripts/` directory mixes build, runtime materialization, packaging, verification and developer helpers.

Target ownership:

```text
tools/build/
tools/runtime/
tools/package/
tools/verify/
tools/dev/windows/
```

Migration rule: never move a script without searching and updating every workflow, CMake reference, command launcher and documentation reference in the same commit.

### 5.5 Keep product source tree stable unless ownership is wrong

`src/native/src/{app,ai,desktop,harness,search,ui}` already expresses useful domain ownership. Do not churn source paths for aesthetics.

The important follow-up is build-graph ownership: common implementation files currently compiled separately into multiple executables should become explicit libraries/object libraries where that improves dependency direction and reduces duplicate compile ownership.

## 6. Runtime V3 dependency model

Runtime V2 creates Harness and Pi independently, producing two large dependency trees. Runtime V3 creates one Windows Agent workspace.

Conceptual workspace:

```json
{
  "private": true,
  "dependencies": {
    "@deepseek-ai/dsh": "<pinned>",
    "@earendil-works/pi-coding-agent": "<pinned>"
  }
}
```

Generation model:

```text
runtime workspace lock
        |
        +-- npm ci --omit=dev (per architecture where required)
        |
        +-- one dependency graph
        +-- one top-level node_modules
        +-- npm-compatible dedupe/hoist at generation time
        +-- explicit production metadata prune
        +-- runtime manifest generation
        +-- Pi CLI probe
        +-- DSH CLI probe
        +-- DSH Web probe
        +-- path/file-count audit
        +-- immutable Runtime V3 archive
```

There must be no normal packaging step whose job is to repair two independently generated dependency trees after they have already been installed.

## 7. Runtime V3 installed layout

Target product layout:

```text
MiaoDesk/
  MiaoDesk.exe
  Assets/
  Wallpapers/

  Runtime/
    Hosts/
      MiaoDeskWallpaper.exe
      MiaoDeskHarness.exe       # retained only if process review keeps it

    Node/
      node.exe

    Agent/
      package.json
      package-lock.json
      node_modules/
      entries/
        dsh-entry.mjs
        pi-entry.mjs

    Native/
      goz.exe
      gozd.exe

    runtime-manifest.json
```

Only `MiaoDesk.exe` is product-facing. Internal hosts and third-party runtimes have explicit ownership.

The final exact names can be shortened if MAX_PATH projections require it, but semantic ownership takes priority over arbitrary flattening.

## 8. Production prune policy

Runtime V3 uses an allow/deny classification maintained as code and documented in the manifest.

Potentially removable categories include development-only documentation, examples/tests and declaration-source-map metadata when verified not to participate in runtime behavior.

Never remove by blanket extension without package-aware verification:

- `.js`, `.mjs`, `.cjs`, `.json`, `.node`, `.wasm` are runtime-sensitive;
- `.d.ts` may be retained unless a package-aware rule proves it unnecessary;
- source maps may be retained for production diagnostics unless explicitly classified otherwise;
- package-specific assets must be treated according to that package's runtime entry graph.

Prune results must be followed by real runtime probes.

## 9. File-count and path budgets

The existing approximately 46k-file x64 product is a baseline failure signal, not the long-term target.

V3 gates:

- Phase 1 target: no duplicated top-level Pi/Harness dependency trees;
- initial package target: <= 25,000 files;
- preferred mature target: <= 15,000 files, provided this does not require unsafe bundling or dependency mutation;
- 0 paths above the projected 248-character ceiling;
- no dependency on Win32 long-path policy;
- file-count reports are emitted by CI by ownership group (`product`, `agent`, `native`, `wallpapers`).

File count alone is not a reason to delete runtime files. Correctness gates always dominate size targets.

## 10. Migration phases

### Phase 0 - Inventory and freeze the baseline

Deliverables:

- repository top-level ownership inventory;
- script/reference graph inventory;
- current executable classification;
- current x64/ARM64 runtime manifest comparison;
- baseline file-count, size and path-depth report.

Acceptance:

- current main continues building;
- no product behavior change.

### Phase 1 - Repository physical layout cleanup

Order:

1. move WebView2 SDK out of `runtime/<arch>`;
2. introduce `runtime/manifests/` ownership;
3. create `tools/{build,runtime,package,verify,dev}`;
4. move root developer launchers;
5. migrate scripts in small responsibility-based batches;
6. update CMake/workflows/docs/contracts after every batch;
7. add a repository-layout verification contract.

Acceptance:

- x64 and ARM64 configure/build still work;
- no workflow references obsolete paths;
- no build SDK exists under an end-user runtime ownership directory;
- repository root contains only genuine project-level entry files.

### Phase 2 - Native target/process normalization

Deliverables:

- explicit CMake libraries/object libraries for stable shared domains where useful;
- production/internal/diagnostic target labels;
- install components: `Product`, `InternalHosts`, `Diagnostics`;
- diagnostics excluded from normal install;
- evaluate whether `MiaoDeskHarness.exe` remains an independent process.

Acceptance:

- normal install exposes one user entry executable;
- wallpaper crash/restart isolation remains valid;
- diagnostic executable is CI-only;
- no behavioral regressions in wallpaper or agent startup.

### Phase 3 - Unified Agent workspace

Deliverables:

- shared `runtime/workspace/package.json` + lock;
- one production dependency graph for DSH + Pi;
- explicit entry wrappers;
- architecture-aware generation for native optional dependencies;
- deterministic Runtime V3 manifest.

Acceptance:

- Pi CLI works;
- DSH CLI works;
- DSH Web works;
- API/provider behavior remains unchanged;
- duplicate identical package count falls substantially.

### Phase 4 - Runtime V3 archive and installer integration

Deliverables:

- one Agent runtime archive per Windows architecture when architecture-specific content exists;
- one canonical staging function used by portable, NSIS and MSIX paths;
- remove late ad-hoc dependency hoist repair from normal packaging;
- explicit production prune manifest;
- update/rollback layer consumes Runtime V3 atomically.

Acceptance:

- moved-install DSH Web startup passes;
- stock-Windows 248 path budget passes;
- package contains no build SDK/source/intermediate trees;
- x64 and ARM64 product layouts are logically identical.

### Phase 5 - Cleanup legacy V2

Only after V3 package/upgrade testing passes:

- remove old per-agent archive format;
- remove compatibility shims no longer referenced;
- remove obsolete scripts/workflows/docs;
- bump runtime manifest schema;
- keep rollback compatibility for the last released V2 package where updater behavior requires it.

## 11. CI gates after the migration

Required fast contracts:

- repository layout contract;
- source-tree external-build contract;
- no build SDK under runtime bundle paths;
- no root-level implementation/temporary packaging artifacts;
- install manifest references only approved product/runtime roots.

Required package contracts:

- real x64 package build;
- real ARM64 package build;
- file-count ownership report;
- projected 85-character install-root MAX_PATH audit;
- relocated install test;
- Pi CLI startup;
- DSH CLI startup;
- DSH Web HTTP startup;
- wallpaper process startup;
- no diagnostic executables in production package.

## 12. Rollback strategy

Repository physical moves are performed in independently buildable commits. Runtime V3 does not delete V2 inputs until the V3 materializer and package tests are green.

During transition:

- native runtime resolution may temporarily support V2 and V3 locations;
- V3 is preferred when present;
- V2 remains fallback only until installer/updater migration is proven;
- compatibility fallback is removed in Phase 5, not earlier.

## 13. Immediate execution order

The next work should proceed in this exact order:

1. inventory and classify current top-level repository files;
2. move WebView2 build SDK ownership out of `runtime/<arch>`;
3. create the `tools/` responsibility tree and migrate verification/build/package scripts in controlled batches;
4. clean root-level CMD helpers;
5. add repository-layout CI contract;
6. normalize CMake target/install-component ownership;
7. only then build the unified Agent workspace and Runtime V3 package structure.

Do not resume package-size micro-optimization before Phase 1 repository cleanup is complete.
