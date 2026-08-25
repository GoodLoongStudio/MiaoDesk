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

The PowerShell runner adds one phase-specific recovery proof that cannot be inferred from Widget health alone. After a successful `search` phase it records the current interactive-session `explorer.exe` PID set in `%LOCALAPPDATA%\TuringDesk\Diagnostics\widget-acceptance-search.explorer-pids`. Before the `explorer` phase is allowed to invoke the Widget probe, the current-session Explorer PID set must be non-empty and different from that checkpoint. A fresh `baseline` clears any stale Explorer checkpoint. This makes "Explorer restart" an observed recovery event rather than a label that could be passed without actually restarting Explorer.

Explorer restart evidence is deliberately process-level diagnostics only. The runner does not discover Progman/WorkerW, enumerate shell HWNDs, call `SetParent`, or change z-order. Windows desktop attachment ownership remains exclusively in `DesktopShellHost`.

Stable probe exit codes are `BaselineMissing = 65`, `BaselineMismatch = 66`, and `SequenceOutOfOrder = 67`. The PowerShell runner additionally fails before the `explorer` probe when Explorer restart evidence is missing or unchanged. These failures require the operator to perform the missing recovery action rather than silently skipping evidence.

PID/HWND values are intentionally excluded from the Widget identity set. Explorer restart and runtime recovery may legitimately recreate processes and HWNDs, while the configured Widget identity must remain stable. The separate explorer.exe PID checkpoint is evidence that the shell process restarted; it is not used as Widget identity and is not product runtime state.

The sequence cursor and Explorer checkpoint are diagnostics-only evidence. They are never consumed by Wallpaper/Widget behavior. Removing the diagnostics directory resets acceptance evidence without affecting configured Widgets.

Passing these probes does not replace visual confirmation. The operator must still verify that the Widget is above the TuringDesk wallpaper, below desktop icons, remains visible while Settings/Search are open, returns after Explorer restart, and restores to the correct display after monitor reconnect/change.
