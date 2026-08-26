# M3 Widget acceptance evidence seal

Status: active M3 real-Windows acceptance contract.

The interactive Widget acceptance flow remains:

```text
baseline -> settings -> search -> explorer -> monitor
```

Passing probes and CI are not visual product acceptance. A real ARM64 Windows operator must still visually confirm that the Widget stays above the TuringDesk wallpaper, below desktop icons, remains visible while Settings/Search are open, recovers after Explorer restart, and returns to the correct monitor/geometry after display topology changes.

## Durable evidence set

The successful `baseline` phase now fingerprints the exact `TuringDeskWidgetAcceptance.exe` used to start the sequence. The runner writes `widget-acceptance-binary.sha256` with the executable SHA-256 and byte length. Every later phase refuses to run if that executable differs. This prevents reports from different acceptance builds being combined into one apparently valid sequence.

After the `monitor` phase succeeds, run:

```powershell
.\scripts\seal-widget-acceptance-evidence.ps1
```

The sealer refuses to run unless the acceptance sequence cursor is exactly `monitor`. It requires one coherent evidence set containing:

- baseline Widget identity set;
- ordered sequence cursor;
- acceptance binary SHA-256/length checkpoint;
- all five phase health reports;
- all five full virtual-desktop screenshots and their SHA-256 sidecars;
- Explorer PID restart checkpoint;
- monitor topology checkpoint and observed topology-transition evidence.

It writes:

```text
%LOCALAPPDATA%\TuringDesk\Diagnostics\widget-acceptance-evidence.manifest.json
%LOCALAPPDATA%\TuringDesk\Diagnostics\widget-acceptance-evidence.manifest.sha256
```

The manifest schema is `turingdesk.widget-acceptance-evidence.v1`. Every required evidence file is recorded with file name, byte length, SHA-256 and last-write UTC timestamp. The manifest also records the acceptance binary identity, baseline Widget identity set, Windows session id, OS version and process architecture.

The sealer immediately runs the independent verifier after writing the manifest. A seal is therefore not considered produced unless the just-written package can be re-read and validated successfully.

## Independent verification

A previously sealed package can be checked again at any time with:

```powershell
.\scripts\verify-widget-acceptance-evidence.ps1
```

The verifier recomputes the manifest SHA-256, then checks every recorded evidence file for continued existence, byte length and SHA-256 equality. It additionally checks:

- the manifest and seal use schema `turingdesk.widget-acceptance-evidence.v1`;
- the sequence remains completed at `monitor`;
- the acceptance binary checkpoint is present and still matches the binary SHA-256/length recorded in the manifest;
- all five `baseline/settings/search/explorer/monitor` report, PNG and PNG sidecar artifacts remain present;
- each PNG still matches its sidecar SHA-256 and phase metadata;
- visual sidecars retain `capturedAtUtc` and virtual-desktop bounds;
- `capturedAtUtc` is strictly ordered as `baseline < settings < search < explorer < monitor`;
- every phase health report was written before its matching screenshot, and the screenshot follows within five minutes;
- no recorded evidence file is timestamped after the manifest seal;
- Explorer restart and monitor-topology recovery evidence remain in the sealed set;
- the baseline Widget identity set still matches the identity set recorded in the manifest.

The chronology, Widget identity and acceptance-binary checks solve different problems. Matching hashes alone can still describe artifacts assembled from different acceptance rounds; stable Widget identities do not prove that the same test binary was used; and using the same binary does not prove the phases ran in order. All three are required.

This catches evidence mutation after sealing rather than merely protecting the manifest itself. It also makes the final evidence package independently auditable without discovering product HWNDs or re-running the runtime probe.

## Ownership boundary

The runner, sealer and verifier are diagnostics-only. They must not discover or mutate Progman, WorkerW, DefView, Widget HWNDs or wallpaper HWNDs, and must not call `SetParent` or `SetWindowPos`. Runtime/surface truth remains owned by `WidgetService` and `DesktopShellHost`; these scripts only fingerprint the acceptance executable and hash/validate already-produced acceptance artifacts.

Static CI guards prevent the runner/verifier from silently losing required binary/integrity/chronology checks or gaining Windows Shell ownership. PowerShell Syntax CI runs those guards on changes to the acceptance scripts.

## Completion rule

A sealed and independently verified manifest, bound to one acceptance binary, is required evidence for closing the M3 real-Windows gate, but it does not itself prove visual correctness. Human review of the five screenshots and the live interactive flow is still mandatory before M3 is marked complete.
