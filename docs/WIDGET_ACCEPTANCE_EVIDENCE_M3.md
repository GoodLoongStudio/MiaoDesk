# M3 Widget acceptance evidence seal

Status: active M3 real-Windows acceptance contract.

The interactive Widget acceptance flow remains:

```text
baseline -> settings -> search -> explorer -> monitor
```

Passing probes and CI are not visual product acceptance. A real ARM64 Windows operator must still visually confirm that the Widget stays above the TuringDesk wallpaper, below desktop icons, remains visible while Settings/Search are open, recovers after Explorer restart, and returns to the correct monitor/geometry after display topology changes.

## Durable evidence set

The successful `baseline` phase fingerprints the exact `TuringDeskWidgetAcceptance.exe` used to start the sequence. The runner writes `widget-acceptance-binary.sha256` with the executable SHA-256 and byte length. Every later phase refuses to run if that executable differs.

The successful baseline also records the enabled Web Widget placement configuration in `widget-acceptance-baseline.config` using schema `turingdesk.widget-acceptance-config.v1`. This snapshot is produced through `WidgetService::List`, not direct Store/INI access, and includes stable Widget id, target `monitorId`, kind, exact normalized x/y/width/height values, zIndex and enabled state. Runtime PID/HWND identity is deliberately excluded because Explorer/runtime recovery may legally recreate those surfaces. Settings/Search/Explorer/monitor phases compare the current service-routed configuration with this baseline before the runtime probe can advance the sequence cursor; any change invalidates the round and requires a new baseline.

The native acceptance probe also writes `widget-acceptance-baseline.session`. All five phase reports must come from that same Windows session. The sealer includes this checkpoint as first-class evidence, records `baselineSessionId` in the manifest, and refuses to seal from a different interactive Windows session.

The `settings` and `search` phases also require observed product-window evidence before their Widget health probe runs. The runner must see `TuringDesk.Native.DesktopLibrary` from `TuringDeskWallpaper` during `settings`, and `TuringDesk.Native.SearchWindow` from `TuringDesk` during `search`, in the same interactive Windows session. Those observations are persisted as:

```text
widget-acceptance-settings.window.json
widget-acceptance-search.window.json
```

Both use schema `turingdesk.widget-window-evidence.v1` and record phase, UTC observation time, process name/PID, session id, class name and title. This proves the Settings/Search phases correspond to actual observed TuringDesk windows rather than labels alone.

After the `monitor` phase succeeds, the operator must explicitly record the human visual gate:

```powershell
.\scripts\confirm-widget-visual-acceptance.ps1 `
  -Reviewer '<name>' `
  -WallpaperBelowWidget `
  -IconsAboveWidget `
  -DesktopIconsUsable `
  -SettingsKeepsWidgetVisible `
  -SearchKeepsWidgetVisible `
  -ExplorerRecoveryVisible `
  -MonitorRecoveryVisible
```

This command is intentionally explicit. It records `widget-acceptance-human-visual.json` plus a SHA-256 sidecar using schema `turingdesk.widget-visual-acceptance.v1`. The attestation is bound to the exact acceptance binary and to the SHA-256 of all five phase screenshots, so a review from another run cannot be silently reused.

**Human review** is a distinct gate from runtime health and screenshot hashing. The reviewer must inspect the live interactive desktop and/or the exact five hashed screenshots before recording the attestation; a script-generated or CI-only attestation is not acceptable evidence.

Only after that attestation exists may the evidence package be sealed:

```powershell
.\scripts\seal-widget-acceptance-evidence.ps1
```

The sealer refuses to run unless the sequence cursor is exactly `monitor`, the baseline Windows session matches the sealing process, and the human visual attestation matches the current binary and screenshot set. It requires:

- baseline Widget identity set;
- baseline Widget placement configuration checkpoint (`widget-acceptance-baseline.config`);
- baseline Windows session checkpoint (`widget-acceptance-baseline.session`);
- ordered sequence cursor;
- acceptance binary SHA-256/length checkpoint;
- observed Settings/Search product window evidence;
- all five phase health reports;
- all five full virtual-desktop screenshots and their SHA-256 sidecars;
- Explorer PID restart checkpoint;
- monitor topology checkpoint and observed topology-transition evidence;
- explicit human visual acceptance JSON and its SHA-256 sidecar.

It writes:

```text
%LOCALAPPDATA%\TuringDesk\Diagnostics\widget-acceptance-evidence.manifest.json
%LOCALAPPDATA%\TuringDesk\Diagnostics\widget-acceptance-evidence.manifest.sha256
```

The manifest schema is `turingdesk.widget-acceptance-evidence.v1`. Every required evidence file is recorded with file name, byte length, SHA-256 and last-write UTC timestamp. The manifest also records a `placementConfig` identity containing the baseline config file name, SHA-256 and byte length, acceptance binary identity, `baselineSessionId`, observed Settings/Search evidence mapping, baseline Widget identity set, Windows session/machine metadata and the human reviewer/timestamp.

The sealer immediately runs the independent verifier after writing the manifest.

## Independent verification

A sealed package can be checked again with:

```powershell
.\scripts\verify-widget-acceptance-evidence.ps1
```

The verifier recomputes the manifest SHA-256 and checks every recorded file for existence, byte length and SHA-256 equality. It explicitly verifies `manifest.placementConfig` still points to `widget-acceptance-baseline.config` and independently compares that file's SHA-256 and byte length with the sealed manifest. Replacing or editing the placement checkpoint after sealing invalidates the evidence package.

It also invokes:

```powershell
.\scripts\verify-widget-acceptance-session-evidence.ps1
```

That helper independently proves the five health reports are healthy phase reports from the same Windows session recorded at baseline. For each phase it requires the expected `baselineStatus` / `sessionStatus`, matching `sessionId`, at least one enabled Web Widget, `runtimeReported=true`, `runtimeHealthy=true`, and explicitly requires every enabled Widget section to report all of:

```text
monitorValid=true
geometryValid=true
visible=true
zOrderValid=true
renderingHealthy=true
```

The verifier additionally checks:

- the completed `monitor` sequence;
- stable sealed placement configuration identity;
- acceptance binary continuity;
- sealed baseline Windows-session continuity;
- observed Settings/Search product-window schema, expected process/class, same-session binding and chronology;
- all five report/PNG/sidecar groups;
- screenshot sidecar metadata and strict phase chronology;
- report-before-screenshot timing;
- Explorer restart and monitor topology recovery evidence;
- baseline Widget identity continuity;
- human attestation schema/hash/reviewer/timestamp;
- all seven human confirmations are true;
- human attestation binary identity matches the manifest;
- human-attested screenshot hashes still match all five sealed PNGs;
- visual review occurred after the final monitor screenshot and before sealing.

Widget identity, stable placement configuration, acceptance binary identity, same Windows session, observed Settings/Search windows, healthy phase reports, phase chronology and human visual attestation solve different problems; all are required.

## Ownership boundary

The runner, confirmer, sealer and verifiers are diagnostics-only. The placement checkpoint is generated by a native helper under `src/native/src/desktop/widgets/` that reads configuration only through `WidgetService`. Acceptance code must not regain direct `DesktopWidgetStore`, INI, Progman, WorkerW, DefView, Widget HWND or wallpaper HWND ownership, and must not call `SetParent` or `SetWindowPos`. The foreground-window observation reads the currently active product window only; it does not become a shell attachment implementation. Runtime/surface truth remains owned by `WidgetService` and `DesktopShellHost`.

Static CI guards prevent these tools from silently losing integrity checks or gaining persistence/Windows Shell ownership.

## Completion rule

A sealed and independently verified manifest is required evidence for closing the M3 real-Windows gate, and sealing now requires stable Widget placement configuration plus explicit human review of the same five screenshots and interactive recovery flow. CI still cannot substitute for the real ARM64 Windows visual acceptance itself.