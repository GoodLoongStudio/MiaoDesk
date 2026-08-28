# M3 Widget acceptance sequence continuity

Status: active real-Windows acceptance contract
Date: 2026-08-26

The phase-labelled Widget acceptance probe verifies continuity across the full real-Windows M3 flow instead of treating each phase as an unrelated snapshot.

`baseline` records the sorted enabled Widget identity set in `%LOCALAPPDATA%\TuringDesk\Diagnostics\widget-acceptance-baseline.ids`. It also records the current interactive Windows session id in `%LOCALAPPDATA%\TuringDesk\Diagnostics\widget-acceptance-baseline.session` and a service-routed placement configuration snapshot in `%LOCALAPPDATA%\TuringDesk\Diagnostics\widget-acceptance-baseline.config`. Later `settings`, `search`, `explorer`, and `monitor` phases must match the same Widget identity set, the same placement configuration, and the same Windows session while also passing `WidgetService::GetRuntimeHealth()`.

The placement checkpoint uses schema `turingdesk.widget-acceptance-config.v2` and is built through `WidgetService::List`, never direct `DesktopWidgetStore` or INI access. It includes enabled Web Widget id, `monitorId`, kind, normalized x/y/width/height, zIndex and enabled state. PID/HWND values are excluded because recovery may legitimately recreate them. A later phase compares the current configuration before the runtime probe can advance the sequence cursor. Moving, resizing, reassigning monitor, changing zIndex, changing kind, disabling or replacing the configured Widget after baseline invalidates the round with the existing baseline mismatch contract and requires a fresh baseline.

Each report includes `baselineStatus`, `sessionStatus`, `sessionId`, `sequenceStatus`, and `surfaceIds`. `baselineStatus=recorded` means the baseline Widget identity was written; `matched` means the current Widget ids match it; `missing` and `mismatch` are hard failures. `sessionStatus=recorded` records the baseline Windows session, while `sessionStatus=matched` proves the current phase is still running in that same interactive Windows session. A missing or changed session is treated as a baseline continuity failure and requires a fresh `baseline`.

Successful phases also advance a durable sequence cursor at `%LOCALAPPDATA%\TuringDesk\Diagnostics\widget-acceptance-sequence.phase`. The probe requires the exact previous successful phase before it accepts the next one:

```text
baseline -> settings -> search -> explorer -> monitor
```

This prevents incomplete acceptance evidence such as running `baseline` and jumping directly to `explorer` or `monitor`. A failed phase never advances the sequence cursor, so the operator must first restore healthy Widget runtime state and rerun that phase successfully.

The same-session requirement prevents another class of invalid evidence: a baseline captured in one RDP/console/login session cannot be combined with later phases from another interactive Windows session even when the same persisted Widget ids and the same binary are present. PID/HWND recreation inside one session remains allowed; the session contract is about the interactive desktop context, not runtime-process identity.

A fresh `baseline` is also an explicit evidence reset boundary. Before probing the new baseline, the runner removes stale Explorer/monitor checkpoints, the old placement configuration checkpoint, old phase health reports, old virtual-desktop screenshots/hash sidecars, old observed product window evidence, and any previously sealed `widget-acceptance-evidence.manifest.json` / `.sha256`. The Widget persistence store is not touched. This prevents a previous successful M3 package or placement snapshot from remaining next to a newly-started but incomplete acceptance round.

## Observed Settings/Search product-window evidence

The `settings` and `search` phases are no longer accepted as labels alone. Before the Widget health probe runs, the runner must observe the expected TuringDesk product window as the foreground window in the same interactive Windows session:

```text
settings -> class TuringDesk.Native.DesktopLibrary -> process TuringDeskWallpaper
search   -> class TuringDesk.Native.SearchWindow  -> process TuringDesk
```

The runner waits for the required foreground product window and writes `%LOCALAPPDATA%\TuringDesk\Diagnostics\widget-acceptance-settings.window.json` or `widget-acceptance-search.window.json` using schema `turingdesk.widget-window-evidence.v1`. Each file records the phase, UTC observation time, process name/PID, Windows session id, class name and window title. The later Widget health probe then proves the same configured Widget set and placement configuration are still healthy after the required product window was actually observed.

This observed product window evidence is read-only acceptance instrumentation. It uses `GetForegroundWindow`, window metadata and process metadata only. It does not discover Progman/WorkerW/DefView, mutate a product HWND, call `SetParent`, call `SetWindowPos`, or repair z-order. `DesktopShellHost` remains the sole desktop attachment owner.

## Explorer restart evidence

The PowerShell runner adds phase-specific recovery proof that cannot be inferred from Widget health alone. After a successful `search` phase it records the current interactive-session `explorer.exe` PID set in `%LOCALAPPDATA%\TuringDesk\Diagnostics\widget-acceptance-search.explorer-pids`. Before the `explorer` phase is allowed to invoke the Widget probe, the current-session Explorer PID set must be non-empty and different from that checkpoint. A fresh `baseline` clears any stale Explorer checkpoint. This makes "Explorer restart" an observed recovery event rather than a label that could be passed without actually restarting Explorer.

Explorer restart evidence is deliberately process-level diagnostics only. The runner does not discover Progman/WorkerW, enumerate shell HWNDs, call `SetParent`, or change z-order. Windows desktop attachment ownership remains exclusively in `DesktopShellHost`.

## Monitor recovery evidence

After a successful `explorer` phase, the runner records a canonical snapshot of the current Windows display topology in `%LOCALAPPDATA%\TuringDesk\Diagnostics\widget-acceptance-explorer.monitor-topology`. The snapshot contains each `System.Windows.Forms.Screen` device name, desktop-space bounds and primary-display flag.

The `monitor` phase no longer means "run the health probe with a monitor label". While that phase is running, the runner must observe the display topology change from the explorer checkpoint and then remain stable for several consecutive samples before it invokes `TuringDeskWidgetAcceptance.exe --phase=monitor`. This supports either a physical display disconnect/reconnect or a Windows display-layout change. Before the runtime health probe can advance the phase, the acceptance executable verifies the persisted Widget placement configuration still exactly matches the baseline. The Widget-domain health probe then proves that the same unchanged configured Widget is rendering healthy on its intended target monitor/geometry after the topology transition.

A successful topology observation is persisted in `%LOCALAPPDATA%\TuringDesk\Diagnostics\widget-acceptance-monitor.topology-transition`. It records the UTC observation time plus the topology before the transition, the first changed topology, and the final stable topology. This keeps proof of a disconnect/reconnect even when the final stable topology is identical to the original checkpoint. A fresh `baseline` clears stale monitor topology and transition evidence.

If no topology transition is observed, the monitor phase fails before the Widget probe and the sequence cursor does not advance. If the Widget placement configuration changed after baseline, the phase also fails before the runtime probe. This turns monitor recovery into observed, durable evidence of recovery from the original configuration rather than operator assertion or a manually corrected placement.

Monitor evidence is display-metadata-only diagnostics. It uses `System.Windows.Forms.Screen`; it does not discover Progman/WorkerW/DefView, enumerate runtime HWNDs, call `SetParent`, call `SetWindowPos`, or repair z-order. `DesktopShellHost` remains the sole Windows desktop attachment owner.

## Visual evidence

After each phase passes the Widget-domain health probe, the runner captures the complete Windows virtual desktop into `%LOCALAPPDATA%\TuringDesk\Diagnostics\widget-acceptance-<phase>.png`. The capture spans all screens, including negative virtual desktop coordinates, so multi-monitor placement is preserved instead of cropping to the primary display.

Each PNG has a sibling `%LOCALAPPDATA%\TuringDesk\Diagnostics\widget-acceptance-<phase>.png.sha256` evidence file containing the SHA-256 digest, capture UTC time, phase name and captured virtual desktop bounds. A fresh `baseline` removes stale PNG/hash evidence before beginning a new sequence. The hash is not a semantic visual test; it makes the exact screenshot used for review durable and tamper-evident within the diagnostics bundle.

Screen capture is deliberately read-only acceptance instrumentation. It uses `System.Drawing.Graphics.CopyFromScreen` and display metadata only. It does not discover Progman/WorkerW/DefView, enumerate product HWNDs, mutate parents or change z-order. All Windows desktop attachment ownership remains in `DesktopShellHost`.

The screenshot evidence makes later review reproducible, but it does not replace human visual acceptance. The reviewer still has to inspect the images (or the live desktop) and confirm that Widget pixels are above the TuringDesk wallpaper, below desktop icons, still present with Settings/Search open, restored after Explorer restart, and positioned correctly after the monitor transition.

Stable probe exit codes are `BaselineMissing = 65`, `BaselineMismatch = 66`, and `SequenceOutOfOrder = 67`. Missing/changed Widget identity, missing/changed placement configuration, or missing/changed baseline Windows session use the baseline continuity failures rather than introducing parallel product state. The PowerShell runner additionally fails before the `settings`/`search` probes when the required observed product window is absent, before the `explorer` probe when Explorer restart evidence is missing or unchanged, and before the `monitor` probe when monitor recovery evidence is missing or no display topology transition is observed. These failures require the operator to perform the missing product/recovery action rather than silently skipping evidence.

PID/HWND values are intentionally excluded from both Widget identity and placement configuration continuity. Explorer restart and runtime recovery may legitimately recreate processes and HWNDs, while the configured Widget identity and persisted placement intent must remain stable. The separate explorer.exe PID checkpoint is evidence that the shell process restarted; it is not used as Widget identity and is not product runtime state. The display topology checkpoint and transition evidence are similarly diagnostics-only and are never used as Widget persistence state. The baseline Windows session and placement checkpoints are diagnostics-only evidence and never mutate Widget configuration.

The sequence cursor, baseline Windows session checkpoint, baseline placement configuration checkpoint, observed Settings/Search product window evidence, Explorer checkpoint, display-topology checkpoint, topology-transition evidence and visual evidence are diagnostics-only. They are never consumed by Wallpaper/Widget behavior. Removing the diagnostics directory resets acceptance evidence without affecting configured Widgets.

Passing these probes does not replace visual confirmation. The operator must still verify that the Widget is above the TuringDesk wallpaper, below desktop icons, remains visible while Settings/Search are open, returns after Explorer restart, and restores to the correct display after monitor reconnect/change.