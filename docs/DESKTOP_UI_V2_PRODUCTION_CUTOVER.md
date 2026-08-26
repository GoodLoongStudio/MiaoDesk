# Desktop Library V2 production cutover

Status: active production contract
Date: 2026-08-26

## Why this cutover exists

Real-Windows validation was ambiguous while the repository contained a legacy shipping Desktop Library and a separately compiled V2 candidate. A tester could not reliably tell whether a Widget, wallpaper or navigation result came from the current product shell or the retired UI.

The production Desktop Library now has one implementation only:

```text
TuringDeskWallpaper
  -> WallpaperLibraryWindowProduction.cpp
  -> WallpaperLibraryWindowV2.cpp
  -> DesktopWidgetController / domain callbacks
  -> DesktopControlService and domain services
```

`src/native/src/ui/wallpaper/WallpaperLibraryWindow.cpp` is retired and physically removed. It must not return as a fallback or compatibility implementation.

## M3 / M4 boundary

This cutover does **not** declare M4 complete. It removes UI ambiguity so M3 Widget visible-runtime acceptance can be performed against the same product shell that will continue forward.

V2 already owns the production wallpaper-library and Widget pages. Product sections that still delegate through the `WallpaperSettingsSection` navigation contract remain migration work for M4 and later milestones; they must not be replaced by a second legacy Desktop Library shell.

## Widget diagnostics

The V2 Widget page reads runtime health through `DesktopWidgetController -> DesktopControlService -> WidgetService`. Refreshing Widget health appends a diagnostic snapshot to:

```text
Desktop\TuringDesk-Logs\widget-runtime.log
```

The log records persisted Widget configuration plus runtime PID/HWND, parent/style/visibility, monitor/geometry, WebView2 lifecycle, z-order, issue code and recommended action. The logging path is observational only and does not regain Store, INI or Shell mutation ownership.

## Guard rules

Build contracts must reject all of the following:

- restoring `WallpaperLibraryWindow.cpp`;
- production bridge compilation of the retired legacy UI;
- V2 direct access to `DesktopWidgetStore`, private INI state or desktop Shell mutation;
- Widget diagnostics directly enumerating/mutating desktop HWNDs;
- replacing V2 with a second production-looking validation UI.

## Acceptance rule

CI proves build/architecture stability only. M3 still requires real ARM64 Windows confirmation that the Widget is visible above the wallpaper and below icons, remains visible through Settings/Search, recovers after Explorer restart, and returns to the correct monitor/geometry after display topology changes.
