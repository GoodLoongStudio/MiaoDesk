# M3 Widget acceptance sequence continuity

Status: active real-Windows acceptance contract
Date: 2026-08-26

The phase-labelled Widget acceptance probe verifies continuity across the full real-Windows M3 flow instead of treating each phase as an unrelated snapshot.

`baseline` records the sorted enabled Widget identity set in `%LOCALAPPDATA%\TuringDesk\Diagnostics\widget-acceptance-baseline.ids`. Later `settings`, `search`, `explorer`, and `monitor` phases must match the same identity set while also passing `WidgetService::GetRuntimeHealth()`.

Each report includes `baselineStatus`, `sequenceStatus`, and `surfaceIds`. `baselineStatus=recorded` means the baseline was written; `matched` means the current Widget ids match it; `missing` and `mismatch` are hard failures.

Successful phases also advance a durable sequence cursor at `%LOCALAPPDATA%\TuringDesk\Diagnostics\widget-acceptance-sequence.phase`. The probe requires the exact previous successful phase before it accepts the next one:

```text
baseline -> settings -> search -> explorer -> monitor
```

This prevents incomplete acceptance evidence such as running `baseline` and jumping directly to `explorer` or `monitor`. A failed phase never advances the sequence cursor, so the operator must first restore healthy Widget runtime state and rerun that phase successfully.

## Explorer restart evidence

The PowerShell runner adds phase-specific recovery proof that cannot be inferred from Widget health alone. After a successful `search` phase it records the current interactive-session `explorer.exe` PID set in `%LOCALAPPDATA%\TuringDesk\Diagnostics\widget-acceptance-search.explorer-pids`. Before the `explorer` phase is allowed to invoke the Widget probe, the current-session Explorer PID set must be non-empty and different from that checkpoint. A fresh `baseline` clears any stale Explorer checkpoint. This makes "Explorer restart" an observed recovery event rather than a label that could be passed without actually restarting Explorer.

Explorer restart evidence is deliberately process-level diagnostics only. The runner does not discover Progman/WorkerW, enumerate shell HWNDs, call `SetParent`, or change z-order. Windows desktop attachment ownership remains exclusively in `DesktopShellHost`.

## Monitor recovery evidence

After a successful `explorer` phase, the runner records a canonical snapshot of the current Windows display topology in `%LOCALAPPDATA%\TuringDesk\Diagnostics\widget-acceptance-explorer.monitor-topology`. The snapshot contains each `System.Windows.Forms.Screen` device name, desktop-space bounds and primary-display flag.

The `monitor` phase no longer means "run the health probe with a monitor label". While that phase is running, the runner must observe the display topology change from the explorer checkpoint and then remain stable for several consecutive samples before it invokes `TuringDeskWidgetAcceptance.exe --phase=monitor`. This supports either a physical display disconnect/reconnect or a Windows display-layout change. The Widget-domain health probe then proves that the same configured Widget identity set is still rendering healthy after the topology transition.

A fresh `baseline` clears stale monitor topology evidence. If no topology transition is observed, the monitor phase fails before the Widget probe and the sequence cursor does not advance. This turns monitor recovery into observed evidence rather than operator assertion.

Monitor evidence is display-metadata-only diagnostics. It uses `System.Windows.Forms.Screen`; it does not discover Progman/WorkerW/DefView, enumerate runtime HWNDs, call `SetParent`, call `SetWindowPos`, or repair z-order. `DesktopShellHost` remains the sole Windows desktop attachment owner.

Stable probe exit codes are `BaselineMissing = 65`, `BaselineMismatch = 66`, and `SequenceOutOfOrder = 67`. The PowerShell runner additionally fails before the `explorer` probe when Explorer restart evidence is missing or unchanged, and before the `monitor` probe when monitor recovery evidence is missing or no display topology transition is observed. These failures require the operator to perform the missing recovery action rather than silently skipping evidence.

PID/HWND values are intentionally excluded from the Widget identity set. Explorer restart and runtime recovery may legitimately recreate processes and HWNDs, while the configured Widget identity must remain stable. The separate explorer.exe PID checkpoint is evidence that the shell process restarted; it is not used as Widget identity and is not product runtime state. The display topology checkpoint is similarly diagnostics-only and is never used as Widget persistence state.

The sequence cursor, Explorer checkpoint and display-topology checkpoint are diagnostics-only evidence. They are never consumed by Wallpaper/Widget behavior. Removing the diagnostics directory resets acceptance evidence without affecting configured Widgets.

Passing these probes does not replace visual confirmation. The operator must still verify that the Widget is above the TuringDesk wallpaper, below desktop icons, remains visible while Settings/Search are open, returns after Explorer restart, and restores to the correct display after monitor reconnect/change.
