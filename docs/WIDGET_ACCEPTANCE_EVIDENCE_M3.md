# M3 Widget acceptance evidence seal

Status: active M3 real-Windows acceptance contract.

The interactive Widget acceptance flow remains:

```text
baseline -> settings -> search -> explorer -> monitor
```

Passing probes and CI are not visual product acceptance. A real ARM64 Windows operator must still visually confirm that the Widget stays above the TuringDesk wallpaper, below desktop icons, remains visible while Settings/Search are open, recovers after Explorer restart, and returns to the correct monitor/geometry after display topology changes.

## Durable evidence set

After the `monitor` phase succeeds, run:

```powershell
.\scripts\seal-widget-acceptance-evidence.ps1
```

The sealer refuses to run unless the acceptance sequence cursor is exactly `monitor`. It requires one coherent evidence set containing:

- baseline Widget identity set;
- ordered sequence cursor;
- all five phase health reports;
- all five full virtual-desktop screenshots and their SHA-256 sidecars;
- Explorer PID restart checkpoint;
- monitor topology checkpoint and observed topology-transition evidence.

It writes:

```text
%LOCALAPPDATA%\TuringDesk\Diagnostics\widget-acceptance-evidence.manifest.json
%LOCALAPPDATA%\TuringDesk\Diagnostics\widget-acceptance-evidence.manifest.sha256
```

The manifest schema is `turingdesk.widget-acceptance-evidence.v1`. Every required evidence file is recorded with file name, byte length, SHA-256 and last-write UTC timestamp. The manifest also records the baseline Widget identity set, Windows session id, OS version and process architecture.

This prevents a final acceptance package from silently mixing a screenshot from one run, a health report from another run, and Explorer/monitor evidence from a third run. The manifest hash makes later replacement of any manifest entry detectable.

## Ownership boundary

The sealer is diagnostics-only. It must not discover or mutate Progman, WorkerW, DefView, Widget HWNDs or wallpaper HWNDs, and must not call `SetParent` or `SetWindowPos`. Runtime/surface truth remains owned by `WidgetService` and `DesktopShellHost`; the evidence sealer only hashes already-produced acceptance artifacts.

## Completion rule

A sealed manifest is required evidence for closing the M3 real-Windows gate, but it does not itself prove visual correctness. Human review of the five screenshots and the live interactive flow is still mandatory before M3 is marked complete.
