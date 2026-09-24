# Windows performance baseline

This is the repeatable resource baseline for RC plan issue #60 / P1-2.

The collector follows the full MiaoDesk process tree, not only `MiaoDesk.exe`.
That means wallpaper, Harness, Node and WebView2 descendants are included when
they are launched under a MiaoDesk root process.

## Scenarios

Collect the same four scenarios on the same reference Windows machine:

1. `desktop-only` — MiaoDesk running, wallpaper disabled, no widgets.
2. `wallpaper` — one built-in Scene wallpaper active.
3. `widgets-3` — wallpaper state unchanged, three representative widgets enabled.
4. `ai-idle` — AI/Harness runtime started and idle, with no active generation.

Use at least 20 seconds per scenario after the UI has settled:

```powershell
powershell -ExecutionPolicy Bypass -File packaging\windows\collect-performance-baseline.ps1 \
  -Scenario desktop-only -DurationSeconds 30

powershell -ExecutionPolicy Bypass -File packaging\windows\collect-performance-baseline.ps1 \
  -Scenario wallpaper -DurationSeconds 30

powershell -ExecutionPolicy Bypass -File packaging\windows\collect-performance-baseline.ps1 \
  -Scenario widgets-3 -DurationSeconds 30

powershell -ExecutionPolicy Bypass -File packaging\windows\collect-performance-baseline.ps1 \
  -Scenario ai-idle -DurationSeconds 30
```

Each run writes JSON samples plus a compact text summary.

## Metrics

The report records total MiaoDesk process-tree:

- CPU average / p95 / peak;
- working set average / p95 / peak;
- private memory average / p95 / peak;
- handle average / p95 / peak;
- thread average / peak;
- GPU average / p95 / peak when Windows exposes the GPU performance-counter CIM class;
- process count average / peak.

CPU is calculated from process CPU-time deltas divided by wall time and logical
processor count. GPU is best-effort because some sessions/drivers do not expose
`Win32_PerfFormattedData_GPUPerformanceCounters_GPUEngine`; lack of GPU telemetry
is recorded rather than converted into a false zero.

## Regression comparison

A later build can compare against a saved JSON baseline:

```powershell
powershell -ExecutionPolicy Bypass -File packaging\windows\collect-performance-baseline.ps1 \
  -Scenario wallpaper -DurationSeconds 30 \
  -CompareTo C:\MiaoDeskBaselines\performance-wallpaper.json
```

The command exits with code 2 when a comparable metric crosses both:

- the configured relative regression threshold (30% by default), and
- a noise floor appropriate for the metric.

This prevents an idle CPU baseline such as 0.05% from failing because a later run
measures 0.08%, while still making substantial regressions fail loudly.

Do not create the checked-in release baseline from CI-hosted Windows runners. Their
CPU/GPU/memory topology changes between hosts. Capture the release baseline on the
same physical reference machine used for visual acceptance, record its hardware/OS
alongside the four JSON files, and use CI only to verify the collector itself.
