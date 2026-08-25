# M3 Widget acceptance sequence continuity

Status: active real-Windows acceptance contract
Date: 2026-08-25

The phase-labelled Widget acceptance probe now verifies continuity across the full real-Windows M3 flow instead of treating each phase as an unrelated snapshot.

`baseline` records the sorted enabled Widget identity set in `%LOCALAPPDATA%\TuringDesk\Diagnostics\widget-acceptance-baseline.ids`. Later `settings`, `search`, `explorer`, and `monitor` phases must match the same identity set while also passing `WidgetService::GetRuntimeHealth()`.

Each report includes `baselineStatus` and `surfaceIds`. `baselineStatus=recorded` means the baseline was written; `matched` means the current Widget ids match it; `missing` and `mismatch` are hard failures.

Stable exit codes are `BaselineMissing = 65` and `BaselineMismatch = 66`. The PowerShell runner explains both failures and requires the operator to execute the baseline phase first.

PID/HWND values are intentionally excluded from the identity set. Explorer restart and runtime recovery may legitimately recreate processes and HWNDs, while the configured Widget identity must remain stable. This allows recovery to be proven without falsely requiring stale Windows handles to survive.

The intended real-Windows order remains:

`baseline -> settings -> search -> explorer -> monitor`

Passing these probes does not replace visual confirmation. The operator must still verify that the Widget is above the TuringDesk wallpaper, below desktop icons, remains visible while Settings/Search are open, returns after Explorer restart, and restores to the correct display after monitor reconnect/change.
