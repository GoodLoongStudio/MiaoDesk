# Windows visual acceptance

This is the execution-evidence step for RC plan issue #60 / P0-1.

Run from a normal Windows desktop session after installing or staging MiaoDesk:

```powershell
powershell -ExecutionPolicy Bypass -File packaging\windows\collect-visual-acceptance.ps1 -OpenResult
```

or with PowerShell 7:

```powershell
pwsh -File packaging\windows\collect-visual-acceptance.ps1 -OpenResult
```

The collector creates one timestamped folder on the Desktop containing:

- `desktop.png`: the full Windows virtual desktop screenshot;
- `acceptance.json`: machine-readable process/surface/DPI/geometry/readiness evidence;
- `README.txt`: a compact summary and the remaining human visual checks.

## What is automated

The collector enumerates MiaoDesk desktop surfaces and records:

- Window class/title/HWND/process;
- surface role;
- visibility;
- per-window DPI;
- desktop rectangle and parent window class;
- WebView2 Environment/Controller/Navigation readiness;
- Native Widget PaintReady;
- MiaoDesk process working set/private memory/handle count/CPU time.

Warnings are emitted for invisible surfaces, missing Widget PaintReady, or incomplete Web surface lifecycle telemetry.

## What still needs a human

The collector deliberately does not claim visual correctness from telemetry alone. Check the screenshot/live desktop for:

- MiaoCloud / NeonCity / MysticMoon composition and motion;
- Widget text/borders not clipped;
- widget-on-desktop z-order relative to wallpaper/icons;
- cross-monitor mixed-DPI movement;
- wallpaper disabled while widgets remain visible;
- Explorer restart recovery;
- sleep/resume recovery.

For Explorer restart and sleep/resume, run the collector again and compare the two JSON reports and screenshots.
