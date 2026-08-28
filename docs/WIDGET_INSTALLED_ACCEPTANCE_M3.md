# M3 installed ARM64 Widget acceptance

Status: active real-Windows acceptance entrypoint
Date: 2026-08-27

M3 is not complete when the Widget acceptance executable merely builds in CI. The exact validated ARM64 package installed by `UPDATE-TURINGDESK.cmd` must carry the same `TuringDeskWidgetAcceptance.exe` used by the five-phase real-Windows acceptance sequence.

## Installed package contract

The compact ARM64 artifact contains:

```text
TuringDesk.exe
TuringDeskWallpaper.exe
TuringDeskHarness.exe
TuringDeskWidgetAcceptance.exe
```

`Runtime`, `Pi`, and `Goz` are not duplicated into each compact artifact. The updater reuses the stable shared ARM64 RuntimeBundle through junctions and materializes that bundle only when its manifest key changes.

After a validated update, the acceptance probe is therefore available at:

```text
%LOCALAPPDATA%\TuringDesk\NativeTest\TuringDeskWidgetAcceptance.exe
```

The installed build marker at `%LOCALAPPDATA%\TuringDesk\NativeTest\.installed-build-sha` identifies the validated package that owns the probe.

## Exact-build and architecture gate

The installed wrapper refuses to collect M3 evidence unless the installation can be tied to the exact source revision being reviewed:

- `.installed-build-sha` must exist and contain a valid 40-character Git commit SHA;
- the installed `TuringDeskWidgetAcceptance.exe` must be an ARM64 PE image with PE machine `0xAA64`;
- when the wrapper is run from a Git checkout, the installed build SHA must match the checkout `HEAD` exactly;
- if the installed package and checkout differ, update TuringDesk or switch the checkout before starting/restarting the baseline phase.

This prevents a visually successful acceptance round from being attributed to a different binary or architecture than the exact `main` revision under review.

Before advancing any durable acceptance phase, operators can run a non-destructive readiness check:

```powershell
.\scripts\run-installed-widget-acceptance.ps1 -VerifyOnly
```

`-VerifyOnly` performs the installed-package, build-SHA, checkout-HEAD and ARM64 PE checks, then exits before `run-widget-runtime-acceptance.ps1` is invoked. It does not collect phase evidence and does not advance the durable phase cursor. Use it after an update, after switching the checkout, or before restarting a partially completed M3 acceptance session.

## Operator entrypoint

Run the installed acceptance wrapper from a checkout of the same `main` revision:

```powershell
.\scripts\run-installed-widget-acceptance.ps1 -VerifyOnly
.\scripts\run-installed-widget-acceptance.ps1 -Phase baseline
.\scripts\run-installed-widget-acceptance.ps1 -Phase settings
.\scripts\run-installed-widget-acceptance.ps1 -Phase search
.\scripts\run-installed-widget-acceptance.ps1 -Phase explorer
.\scripts\run-installed-widget-acceptance.ps1 -Phase monitor
```

The wrapper resolves `%LOCALAPPDATA%\TuringDesk\NativeTest`, requires the installed `TuringDeskWidgetAcceptance.exe`, verifies the installed validated build marker and ARM64 PE machine, checks checkout/build identity when Git is available, and delegates to `run-widget-runtime-acceptance.ps1` only when a real phase is requested. It does not bypass the existing M3 continuity rules.

The sequence remains strictly:

```text
baseline -> settings -> search -> explorer -> monitor
```

The same acceptance binary fingerprint, Widget identity set, `turingdesk.widget-acceptance-config.v2` placement configuration, and interactive Windows session must survive the full sequence. Runtime PID/HWND recreation is allowed where recovery legitimately recreates surfaces.

## Real-Windows gate

The installed wrapper does not automate the human portions of M3. A real ARM64 Windows session still must prove:

- the three fixed showcase Widgets (`玻璃时钟`, `今日待办`, `玻璃天气`) render above the TuringDesk wallpaper and below desktop icons;
- desktop icons remain usable;
- Settings and Search remain visible without hiding or pausing the Widgets;
- Explorer restart restores the Widget surfaces;
- a real display topology change restores the original Widget placement intent;
- the five phase screenshots and diagnostics are reviewed by a human in the same Windows session;
- the evidence package is sealed and independently verified.

Only after those checks pass may the M2/M3 real-Windows acceptance gates be closed and work advance to M4. CI, package creation, `-VerifyOnly`, and a successful non-interactive self-test are not substitutes for this gate.
