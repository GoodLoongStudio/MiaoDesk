# M3 Widget acceptance evidence seal

Status: active M3 real-Windows acceptance contract.

The interactive Widget acceptance flow remains:

```text
baseline -> settings -> search -> explorer -> monitor
```

Passing probes and CI are not visual product acceptance. A real ARM64 Windows operator must still visually confirm that the Widget stays above the TuringDesk wallpaper, below desktop icons, remains visible while Settings/Search are open, recovers after Explorer restart, and returns to the correct monitor/geometry after display topology changes.

## Durable evidence set

The successful `baseline` phase fingerprints the exact `TuringDeskWidgetAcceptance.exe` used to start the sequence. The runner writes `widget-acceptance-binary.sha256` with the executable SHA-256 and byte length. Every later phase refuses to run if that executable differs.

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

The sealer refuses to run unless the sequence cursor is exactly `monitor` and the human visual attestation matches the current binary and screenshot set. It requires:

- baseline Widget identity set;
- ordered sequence cursor;
- acceptance binary SHA-256/length checkpoint;
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

The manifest schema is `turingdesk.widget-acceptance-evidence.v1`. Every required evidence file is recorded with file name, byte length, SHA-256 and last-write UTC timestamp. The manifest also records acceptance binary identity, baseline Widget identity set, Windows session/machine metadata and the human reviewer/timestamp.

The sealer immediately runs the independent verifier after writing the manifest.

## Independent verification

A sealed package can be checked again with:

```powershell
.\scripts\verify-widget-acceptance-evidence.ps1
```

The verifier recomputes the manifest SHA-256 and checks every recorded file for existence, byte length and SHA-256 equality. It additionally checks:

- the completed `monitor` sequence;
- acceptance binary continuity;
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

Widget identity, acceptance binary identity, phase chronology and human visual attestation solve different problems; all are required.

## Ownership boundary

The runner, confirmer, sealer and verifier are diagnostics-only. They must not discover or mutate Progman, WorkerW, DefView, Widget HWNDs or wallpaper HWNDs, and must not call `SetParent` or `SetWindowPos`. Runtime/surface truth remains owned by `WidgetService` and `DesktopShellHost`.

Static CI guards prevent these tools from silently losing integrity checks or gaining Windows Shell ownership.

## Completion rule

A sealed and independently verified manifest is required evidence for closing the M3 real-Windows gate, and sealing now requires explicit human review of the same five screenshots and interactive recovery flow. CI still cannot substitute for the real ARM64 Windows visual acceptance itself.
