param()

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

function Require-File([string]$relativePath) {
    $path = Join-Path $root $relativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Missing desktop domain contract file: $relativePath"
    }
    return $path
}

$files = @{
    ControlHeader = 'src/native/include/turingdesk/DesktopControlService.h'
    Control = 'src/native/src/desktop/control/DesktopControlService.cpp'
    WallpaperHeader = 'src/native/include/turingdesk/WallpaperService.h'
    Wallpaper = 'src/native/src/desktop/wallpaper/WallpaperService.cpp'
    WidgetHeader = 'src/native/include/turingdesk/WidgetService.h'
    Widget = 'src/native/src/desktop/widgets/WidgetService.cpp'
    SurfaceTelemetryHeader = 'src/native/include/turingdesk/DesktopSurfaceTelemetry.h'
    SurfaceTelemetry = 'src/native/src/desktop/shell/DesktopSurfaceTelemetry.cpp'
    WidgetControllerHeader = 'src/native/include/turingdesk/DesktopWidgetController.h'
    WidgetController = 'src/native/src/desktop/widgets/DesktopWidgetController.cpp'
    WidgetUiHeader = 'src/native/include/turingdesk/DesktopWidgetUiAdapter.h'
    WidgetUi = 'src/native/src/ui/widgets/DesktopWidgetUiAdapter.cpp'
    AutomationHeader = 'src/native/include/turingdesk/AutomationService.h'
    Automation = 'src/native/src/desktop/automation/AutomationService.cpp'
    AutomationUiHeader = 'src/native/include/turingdesk/AutomationUiAdapter.h'
    AutomationUi = 'src/native/src/ui/automation/AutomationUiAdapter.cpp'
    AutomationWindow = 'src/native/src/ui/automation/WallpaperAutomationWindowProduction.cpp'
    PerformanceHeader = 'src/native/include/turingdesk/PerformanceService.h'
    Performance = 'src/native/src/desktop/performance/PerformanceService.cpp'
    PerformanceUiHeader = 'src/native/include/turingdesk/PerformanceUiAdapter.h'
    PerformanceUi = 'src/native/src/ui/performance/PerformanceUiAdapter.cpp'
    ProductionEngine = 'src/native/src/desktop/wallpaper/legacy/WallpaperEngineProduction.cpp'
    LibraryWindow = 'src/native/src/ui/wallpaper/WallpaperLibraryWindowProduction.cpp'
    PiAdapter = 'src/native/src/ai/tools/DesktopWidgetTools.cpp'
    CMake = 'src/native/CMakeLists.txt'
    Doc = 'docs/DESKTOP_DOMAIN_ARCHITECTURE.md'
    LayoutDoc = 'docs/NATIVE_SOURCE_LAYOUT.md'
}

$text = @{}
foreach ($entry in $files.GetEnumerator()) {
    $path = Require-File $entry.Value
    $text[$entry.Key] = Get-Content -LiteralPath $path -Raw
}

foreach ($marker in @('DesktopControlService', 'GetSnapshot', 'WidgetRuntimeHealth', 'ApplyLibraryItem', 'AssignLibraryItemToMonitor', 'CreateWebWidget', 'ListWidgets')) {
    if (-not $text.ControlHeader.Contains($marker)) { throw "Desktop control header missing marker: $marker" }
}
foreach ($marker in @('wallpaperService.GetState', 'widgetService.List', 'widgetService.GetRuntimeHealth', 'snapshot->widgetRuntime', 'DesktopControlService::ApplyLibraryItem', 'DesktopControlService::AssignLibraryItemToMonitor')) {
    if (-not $text.Control.Contains($marker)) { throw "Desktop control facade missing routing marker: $marker" }
}
foreach ($forbidden in @('WritePrivateProfileStringW', 'DesktopWidgetStore store', 'WallpaperPackage::Validate', 'WallpaperMonitorAssignments assignments')) {
    if ($text.Control.Contains($forbidden)) { throw "Desktop control facade regained domain ownership: $forbidden" }
}

foreach ($marker in @('WallpaperService::GetState', 'WallpaperService::ApplyLibraryItem', 'WallpaperService::AssignLibraryItemToMonitor', 'WallpaperPackage::Validate', 'WallpaperMonitorAssignments assignments')) {
    if (-not $text.Wallpaper.Contains($marker)) { throw "WallpaperService missing ownership marker: $marker" }
}
foreach ($marker in @('WidgetSurfaceHealth', 'WidgetRuntimeHealth', 'surfaces', 'GetRuntimeHealth', 'environmentReported', 'controllerReported', 'navigationReported', 'zOrderReported')) {
    if (-not $text.WidgetHeader.Contains($marker)) { throw "WidgetService header missing runtime health contract: $marker" }
}
foreach ($marker in @('WidgetService::CreateWeb', 'WidgetService::Update', 'WidgetService::Remove', 'WidgetService::GetRuntimeHealth', 'ReadWidgetRuntimeDetail', 'InspectWidgetSurface', 'processRunning', 'hwndReady', 'parentValid', 'childStyleValid', 'visible', 'DesktopWidgetStore store')) {
    if (-not $text.Widget.Contains($marker)) { throw "WidgetService missing ownership marker: $marker" }
}
foreach ($marker in @('WebDesktopSurfaceChild.h', 'HasStructuredLifecycleTelemetry', 'kWebSurfaceRoleProperty', 'kWebSurfaceEnvironmentReadyProperty', 'kWebSurfaceControllerReadyProperty', 'kWebSurfaceNavigationReadyProperty', 'environmentReported = lifecycleTelemetry', 'controllerReported = lifecycleTelemetry', 'navigationReported = lifecycleTelemetry')) {
    if (-not $text.Widget.Contains($marker)) { throw "WidgetService WebView2 lifecycle telemetry contract missing marker: $marker" }
}
foreach ($marker in @('DesktopSurfaceTelemetry.h', 'InspectDesktopSurfaceZOrder', 'DesktopSurfaceTelemetryRole::Widget', 'zOrderReported = zOrder.reported', 'zOrderValid = zOrder.valid')) {
    if (-not $text.Widget.Contains($marker)) { throw "WidgetService z-order telemetry routing missing marker: $marker" }
}
foreach ($marker in @('DesktopSurfaceZOrderHealth', 'InspectDesktopSurfaceZOrder', 'Read-only z-order inspection')) {
    if (-not $text.SurfaceTelemetryHeader.Contains($marker)) { throw "Desktop surface telemetry header missing marker: $marker" }
}
foreach ($marker in @('SHELLDLL_DefView', 'TuringDesk.Native.WallpaperHost', 'TuringDesk.Native.WebWallpaperHost', 'DesktopSurfaceTelemetryRole::Widget', 'wallpaper surface is above Widget', 'Widget surface is below wallpaper')) {
    if (-not $text.SurfaceTelemetry.Contains($marker)) { throw "Desktop surface telemetry implementation missing marker: $marker" }
}
foreach ($forbidden in @('SetParent(', 'SetWindowPos(', 'SendMessageTimeoutW(', '0x052C')) {
    if ($text.SurfaceTelemetry.Contains($forbidden)) { throw "Read-only DesktopSurfaceTelemetry gained shell mutation ownership: $forbidden" }
}

foreach ($marker in @('AutomationService::GetState', 'AutomationService::UpsertPlaylist', 'AutomationService::Evaluate', 'AutomationService::ForceNextPlaylist', 'WallpaperAutomationStore store')) {
    if (-not $text.Automation.Contains($marker)) { throw "AutomationService missing ownership marker: $marker" }
}
foreach ($marker in @('service_.GetState', 'service_.UpsertProfile', 'service_.UpsertPlaylist', 'service_.Evaluate', 'service_.ForceNextPlaylist')) {
    if (-not $text.AutomationUi.Contains($marker)) { throw "AutomationUiAdapter is not service-routed: $marker" }
}
foreach ($forbidden in @('WallpaperAutomationStore store', 'WritePrivateProfileStringW', 'GetPrivateProfileStringW')) {
    if ($text.AutomationUi.Contains($forbidden)) { throw "AutomationUiAdapter regained persistence ownership: $forbidden" }
}
foreach ($marker in @('AutomationUiAdapter.h', '#define WallpaperAutomationStore AutomationUiAdapter', '#include "WallpaperAutomationWindow.cpp"')) {
    if (-not $text.AutomationWindow.Contains($marker)) { throw "Production automation window bridge missing marker: $marker" }
}

foreach ($marker in @('PerformanceService::GetConfig', 'PerformanceService::SaveConfig', 'WritePrivateProfileStringW')) {
    if (-not $text.Performance.Contains($marker)) { throw "PerformanceService missing ownership marker: $marker" }
}
foreach ($marker in @('service_.GetConfig', 'service_.SaveConfig')) {
    if (-not $text.PerformanceUi.Contains($marker)) { throw "PerformanceUiAdapter is not service-routed: $marker" }
}
foreach ($forbidden in @('WritePrivateProfileStringW', 'GetPrivateProfileStringW', 'GetPrivateProfileIntW')) {
    if ($text.PerformanceUi.Contains($forbidden)) { throw "PerformanceUiAdapter regained persistence ownership: $forbidden" }
}
foreach ($marker in @('PerformanceUiAdapter.h', 'PerformanceUiAdapter adapter', '#define WallpaperAutomationStore AutomationUiAdapter', '#include "WallpaperEngine.cpp"')) {
    if (-not $text.ProductionEngine.Contains($marker)) { throw "Production WallpaperEngine bridge missing domain-routing marker: $marker" }
}

# Service members live in the public adapter/controller declarations; implementation
# files are required to demonstrate actual delegation through those members.
foreach ($marker in @('DesktopControlService.h', 'DesktopControlService service_', 'RuntimeHealth')) {
    if (-not $text.WidgetControllerHeader.Contains($marker)) { throw "DesktopWidgetController header missing facade dependency: $marker" }
}
foreach ($marker in @('DesktopWidgetController::Refresh', 'DesktopWidgetController::RuntimeHealth', 'DesktopWidgetController::CreateClock', 'DesktopWidgetController::SetEnabled', 'service_.ListWidgets', 'service_.GetSnapshot', 'service_.CreateWebWidget', 'service_.UpdateWidget', 'service_.RemoveWidget')) {
    if (-not $text.WidgetController.Contains($marker)) { throw "DesktopWidgetController missing facade routing marker: $marker" }
}
foreach ($forbidden in @('DesktopWidgetStore store', 'WritePrivateProfileStringW', 'ShellExecuteW(', 'WallpaperPackage::Validate', 'FindWindowW(', 'FindWindowExW(', 'GetParent(')) {
    if ($text.WidgetController.Contains($forbidden)) { throw "DesktopWidgetController regained domain/runtime ownership: $forbidden" }
}

foreach ($marker in @('DesktopControlService.h', 'DesktopControlService service_', 'RuntimeHealth')) {
    if (-not $text.WidgetUiHeader.Contains($marker)) { throw "DesktopWidgetUiAdapter header missing facade dependency: $marker" }
}
foreach ($marker in @('DesktopWidgetUiAdapter::RuntimeHealth', 'service_.ListWidgets', 'service_.GetSnapshot', 'service_.CreateWebWidget', 'service_.UpdateWidget', 'service_.RemoveWidget')) {
    if (-not $text.WidgetUi.Contains($marker)) { throw "DesktopWidgetUiAdapter missing facade routing marker: $marker" }
}
foreach ($forbidden in @('DesktopWidgetStore store', 'WritePrivateProfileStringW', 'ShellExecuteW(', 'WallpaperPackage::Validate', 'FindWindowW(', 'FindWindowExW(', 'GetParent(')) {
    if ($text.WidgetUi.Contains($forbidden)) { throw "DesktopWidgetUiAdapter regained domain/runtime ownership: $forbidden" }
}
foreach ($marker in @('DesktopWidgetUiAdapter.h', '#define DesktopWidgetStore DesktopWidgetUiAdapter', '#include "WallpaperLibraryWindow.cpp"')) {
    if (-not $text.LibraryWindow.Contains($marker)) { throw "Production WallpaperLibraryWindow bridge missing marker: $marker" }
}

foreach ($marker in @('DesktopControlService.h', 'DesktopControlService service', 'service.GetSnapshot', 'widgetRuntime')) {
    if (-not $text.PiAdapter.Contains($marker)) { throw "Pi desktop tool adapter missing Desktop Control snapshot marker: $marker" }
}
foreach ($forbidden in @('WritePrivateProfileStringW', 'DesktopWidgetStore store', 'ShellExecuteW(', 'WallpaperPackage::Validate', 'FindWindowW(', 'FindWindowExW(', 'GetParent(')) {
    if ($text.PiAdapter.Contains($forbidden)) { throw "Pi desktop tool adapter regained domain/runtime ownership: $forbidden" }
}

$cmake = $text.CMake
foreach ($marker in @(
    'src/desktop/control/DesktopControlService.cpp',
    'src/desktop/shell/DesktopSurfaceTelemetry.cpp',
    'src/desktop/wallpaper/WallpaperService.cpp',
    'src/desktop/widgets/WidgetService.cpp',
    'src/desktop/automation/AutomationService.cpp',
    'src/desktop/performance/PerformanceService.cpp',
    'src/ui/widgets/DesktopWidgetUiAdapter.cpp',
    'src/ui/automation/WallpaperAutomationWindowProduction.cpp',
    'src/ui/wallpaper/WallpaperLibraryWindowProduction.cpp')) {
    if (-not $cmake.Contains($marker)) { throw "Production source ownership missing from CMake: $marker" }
}
if (($cmake.Split('src/desktop/shell/DesktopSurfaceTelemetry.cpp').Count - 1) -lt 2) {
    throw 'DesktopSurfaceTelemetry must be linked into both app and wallpaper targets.'
}
if ($cmake.Contains('src/WallpaperLibraryWindow.cpp') -or $cmake.Contains('src/WallpaperAutomationWindow.cpp')) {
    throw 'Production target must not compile legacy UI implementation files directly.'
}

foreach ($marker in @('UI / Pi / future Editor', 'Desktop Control contract', 'DesktopWidgetTools.cpp', 'WallpaperLibraryWindowV2.cpp', 'WidgetSurfaceHealth')) {
    if (-not $text.Doc.Contains($marker)) { throw "Desktop domain architecture doc missing marker: $marker" }
}
foreach ($marker in @('desktop/control', 'desktop/widgets', 'desktop/automation', 'desktop/performance')) {
    if (-not $text.LayoutDoc.Contains($marker)) { throw "Native source layout doc missing domain marker: $marker" }
}

Write-Host 'Desktop domain contract OK.'
