# M3 Widget acceptance sequence continuity

Status: active real-Windows acceptance contract
Date: 2026-08-25

The phase-labelled Widget acceptance probe verifies continuity across the full real-Windows M3 flow instead of treating each phase as an unrelated snapshot.

`baseline` records the sorted enabled Widget identity set in `%LOCALAPPDATA%\TuringDesk\Diagnostics\widget-acceptance-baseline.ids`. Later `settings`, `search`, `explorer`, and `monitor` phases must match the same identity set while also passing `WidgetService::GetRuntimeHealth()`.

Each report includes `baselineStatus`, `sequenceStatus`, and `surfaceIds`. `baselineStatus=recorded` means the baseline was written; `matched` means the current Widget ids match it; `missing` and `mismatch` are hard failures.

Successful phases also advance a durable sequence cursor at `%LOCALAPPDATA%\TuringDesk\Diagnostics\widget-acceptance-sequence.phase`. The probe requires the exact previous successful phase before it accepts the next one:

```text
baseline -> settings -> search -> explorer -> monitor
```

This prevents incomplete acceptance evidence such as running `baseline` and jumping directly to `explorer` or `monitor`. A failed phase never advances the sequence cursor, so the operator must first restore healthy Widget runtime state and rerun that phase successfully.

Stable exit codes are `BaselineMissing = 65`, `BaselineMismatch = 66`, and `SequenceOutOfOrder = 67`. The PowerShell runner explains all three failures and requires the operator to restart the sequence from the appropriate prior phase rather than silently skipping evidence.

PID/HWND values are intentionally excluded from the identity set. Explorer restart and runtime recovery may legitimately recreate processes and HWNDs, while the configured Widget identity must remain stable. This allows recovery to be proven without falsely requiring stale Windows handles to survive.

The sequence cursor is evidence ordering only; it is not product runtime state and is never consumed by Wallpaper/Widget behavior. Removing the diagnostics directory resets acceptance evidence without affecting configured Widgets.

Passing these probes does not replace visual confirmation. The operator must still verify that the Widget is above the TuringDesk wallpaper, below desktop icons, remains visible while Settings/Search are open, returns after Explorer restart, and restores to the correct display after monitor reconnect/change.
