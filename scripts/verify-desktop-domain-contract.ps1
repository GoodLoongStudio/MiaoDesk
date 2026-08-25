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

$serviceHeader = Require-File 'src/native/include/turingdesk/DesktopControlService.h'
$serviceSource = Require-File 'src/native/src/DesktopControlService.cpp'
$wallpaperHeader = Require-File 'src/native/include/turingdesk/WallpaperService.h'
$wallpaperSource = Require-File 'src/native/src/WallpaperService.cpp'
$widgetHeader = Require-File 'src/native/include/turingdesk/WidgetService.h'
$widgetSource = Require-File 'src/native/src/WidgetService.cpp'
$automationHeader = Require-File 'src/native/include/turingdesk/AutomationService.h'
$automationSource = Require-File 'src/native/src/AutomationService.cpp'
$automationUiAdapterHeader = Require-File 'src/native/include/turingdesk/AutomationUiAdapter.h'
$automationUiAdapterSource = Require-File 'src/native/src/AutomationUiAdapter.cpp'
$automationProductionWindow = Require-File 'src/native/src/WallpaperAutomationWindowProduction.cpp'
$performanceHeader = Require-File 'src/native/include/turingdesk/PerformanceService.h'
$performanceSource = Require-File 'src/native/src/PerformanceService.cpp'
$performanceUiAdapterHeader = Require-File 'src/native/include/turingdesk/PerformanceUiAdapter.h'
$performanceUiAdapterSource = Require-File 'src/native/src/PerformanceUiAdapter.cpp'
$productionEngine = Require-File 'src/native/src/WallpaperEngineProduction.cpp'
$widgetControllerHeader = Require-File 'src/native/include/turingdesk/DesktopWidgetController.h'
$widgetControllerSource = Require-File 'src/native/src/DesktopWidgetController.cpp'
$widgetUiAdapterHeader = Require-File 'src/native/include/turingdesk/DesktopWidgetUiAdapter.h'
$widgetUiAdapterSource = Require-File 'src/native/src/DesktopWidgetUiAdapter.cpp'
$productionWindow = Require-File 'src/native/src/WallpaperLibraryWindowProduction.cpp'
$toolAdapter = Require-File 'src/native/src/DesktopWidgetTools.cpp'
$cmakePath = Require-File 'src/native/CMakeLists.txt'
$docPath = Require-File 'docs/DESKTOP_DOMAIN_ARCHITECTURE.md'

$header = Get-Content -LiteralPath $serviceHeader -Raw
$source = Get-Content -LiteralPath $serviceSource -Raw
$wallpaperHeaderText = Get-Content -LiteralPath $wallpaperHeader -Raw
$wallpaper = Get-Content -LiteralPath $wallpaperSource -Raw
$widget = Get-Content -LiteralPath $widgetSource -Raw
$automationHeaderText = Get-Content -LiteralPath $automationHeader -Raw
$automation = Get-Content -LiteralPath $automationSource -Raw
$automationUiAdapterHeaderText = Get-Content -LiteralPath $automationUiAdapterHeader -Raw
$automationUiAdapter = Get-Content -LiteralPath $automationUiAdapterSource -Raw
$automationProductionWindowText = Get-Content -LiteralPath $automationProductionWindow -Raw
$performanceHeaderText = Get-Content -LiteralPath $performanceHeader -Raw
$performance = Get-Content -LiteralPath $performanceSource -Raw
$performanceUiAdapterHeaderText = Get-Content -LiteralPath $performanceUiAdapterHeader -Raw
$performanceUiAdapter = Get-Content -LiteralPath $performanceUiAdapterSource -Raw
$productionEngineText = Get-Content -LiteralPath $productionEngine -Raw
$widgetControllerHeaderText = Get-Content -LiteralPath $widgetControllerHeader -Raw
$widgetController = Get-Content -LiteralPath $widgetControllerSource -Raw
$widgetUiAdapterHeaderText = Get-Content -LiteralPath $widgetUiAdapterHeader -Raw
$widgetUiAdapter = Get-Content -LiteralPath $widgetUiAdapterSource -Raw
$productionWindowText = Get-Content -LiteralPath $productionWindow -Raw
$adapter = Get-Content -LiteralPath $toolAdapter -Raw
$cmake = Get-Content -LiteralPath $cmakePath -Raw
$doc = Get-Content -LiteralPath $docPath -Raw

foreach ($marker in @('DesktopControlService', 'DesktopSnapshot', 'GetState', 'GetSnapshot', 'ApplyWebPackage', 'ApplyLibraryItem', 'AssignLibraryItemToMonitor', 'ClearMonitorAssignment', 'CreateWebWidget', 'UpdateWidget', 'RemoveWidget', 'ListWidgets')) {
    if (-not $header.Contains($marker)) { throw "Desktop control header missing marker: $marker" }
}
foreach ($marker in @('WallpaperService.h', 'WidgetService.h')) {
    if (-not $header.Contains($marker)) { throw "Desktop facade missing domain dependency: $marker" }
}
foreach ($marker in @('DesktopControlService::GetSnapshot', 'wallpaperService.GetState', 'widgetService.List', 'DesktopControlService::GetState', 'GetSnapshot(&snapshot)', 'DesktopControlService::ApplyWebPackage', 'DesktopControlService::ApplyLibraryItem', 'DesktopControlService::AssignLibraryItemToMonitor', 'DesktopControlService::ClearMonitorAssignment', 'DesktopControlService::CreateWebWidget')) {
    if (-not $source.Contains($marker)) { throw "Desktop control source missing marker: $marker" }
}
foreach ($forbidden in @('WritePrivateProfileStringW', 'DesktopWidgetStore store', 'WallpaperPackage::Validate', 'WallpaperMonitorAssignments assignments')) {
    if ($source.Contains($forbidden)) { throw "Desktop facade regained domain persistence ownership: $forbidden" }
}

foreach ($marker in @('ApplyLibraryItem', 'AssignLibraryItemToMonitor', 'ClearMonitorAssignment')) {
    if (-not $wallpaperHeaderText.Contains($marker)) { throw "Wallpaper service header missing marker: $marker" }
}
foreach ($marker in @('WallpaperService::GetState', 'WallpaperService::ApplyWebPackage', 'WallpaperService::ApplyLibraryItem', 'WallpaperService::AssignLibraryItemToMonitor', 'WallpaperService::ClearMonitorAssignment', 'WritePrivateProfileStringW', 'WallpaperPackage::Validate', 'WallpaperMonitorAssignments assignments')) {
    if (-not $wallpaper.Contains($marker)) { throw "Wallpaper domain service missing ownership marker: $marker" }
}
foreach ($marker in @('WidgetService::CreateWeb', 'WidgetService::Update', 'WidgetService::Remove', 'DesktopWidgetStore store')) {
    if (-not $widget.Contains($marker)) { throw "Widget domain service missing ownership marker: $marker" }
}

foreach ($marker in @('AutomationService', 'GetState', 'SetEnabled', 'SetActivePlaylist', 'ForceNextPlaylist', 'SelfTest', 'MakeId')) {
    if (-not $automationHeaderText.Contains($marker)) { throw "Automation service header missing marker: $marker" }
}
foreach ($marker in @('AutomationService::GetState', 'AutomationService::UpsertPlaylist', 'AutomationService::ForceNextPlaylist', 'AutomationService::SelfTest', 'AutomationService::MakeId', 'WallpaperAutomationStore store')) {
    if (-not $automation.Contains($marker)) { throw "Automation domain service missing ownership marker: $marker" }
}
foreach ($marker in @('AutomationUiAdapter', 'AutomationService.h', 'AutomationService service_', 'SelfTest')) {
    if (-not $automationUiAdapterHeaderText.Contains($marker)) { throw "Automation UI adapter header missing marker: $marker" }
}
foreach ($marker in @('service_.GetState', 'service_.UpsertProfile', 'service_.UpsertPlaylist', 'service_.UpsertSchedule', 'service_.ForceNextPlaylist', 'desktop::AutomationService::SelfTest', 'desktop::AutomationService::MakeId')) {
    if (-not $automationUiAdapter.Contains($marker)) { throw "Automation UI adapter is not routed through AutomationService: $marker" }
}
foreach ($forbidden in @('WallpaperAutomationStore', 'WritePrivateProfileStringW', 'GetPrivateProfileStringW')) {
    if ($automationUiAdapter.Contains($forbidden)) { throw "Automation UI adapter regained persistence ownership: $forbidden" }
}
foreach ($marker in @('AutomationUiAdapter.h', '#define WallpaperAutomationStore AutomationUiAdapter', '#include "WallpaperAutomationWindow.cpp"')) {
    if (-not $automationProductionWindowText.Contains($marker)) { throw "Production automation window bridge missing service-routing marker: $marker" }
}

foreach ($marker in @('PerformanceService', 'WallpaperPerformancePolicy.h', 'GetConfig', 'SaveConfig')) {
    if (-not $performanceHeaderText.Contains($marker)) { throw "Performance service header missing marker: $marker" }
}
foreach ($marker in @('PerformanceService::GetConfig', 'PerformanceService::SaveConfig', 'WritePrivateProfileStringW', 'FullscreenAction', 'IdleThresholdSeconds')) {
    if (-not $performance.Contains($marker)) { throw "Performance domain service missing ownership marker: $marker" }
}
foreach ($marker in @('PerformanceUiAdapter', 'PerformanceService.h', 'PerformanceService service_')) {
    if (-not $performanceUiAdapterHeaderText.Contains($marker)) { throw "Performance UI adapter header missing marker: $marker" }
}
foreach ($marker in @('service_.GetConfig', 'service_.SaveConfig')) {
    if (-not $performanceUiAdapter.Contains($marker)) { throw "Performance UI adapter is not routed through PerformanceService: $marker" }
}
foreach ($forbidden in @('WritePrivateProfileStringW', 'GetPrivateProfileStringW', 'GetPrivateProfileIntW')) {
    if ($performanceUiAdapter.Contains($forbidden)) { throw "Performance UI adapter regained persistence ownership: $forbidden" }
}
foreach ($marker in @('PerformanceUiAdapter.h', 'PerformanceUiAdapter adapter', '#define WallpaperAutomationStore AutomationUiAdapter', '#include "WallpaperEngine.cpp"')) {
    if (-not $productionEngineText.Contains($marker)) { throw "Production wallpaper engine bridge missing domain-routing marker: $marker" }
}
if ($productionEngineText.Contains('PerformanceService service')) {
    throw 'Production WallpaperEngine must route performance controls through PerformanceUiAdapter, not directly through PerformanceService.'
}

foreach ($marker in @('DesktopWidgetController', 'DesktopControlService.h', 'DesktopControlService service_')) {
    if (-not $widgetControllerHeaderText.Contains($marker)) { throw "Widget UI controller header missing marker: $marker" }
}
foreach ($marker in @('DesktopWidgetController::Refresh', 'DesktopWidgetController::CreateClock', 'DesktopWidgetController::SetEnabled', 'service_.ListWidgets', 'service_.CreateWebWidget', 'service_.UpdateWidget')) {
    if (-not $widgetController.Contains($marker)) { throw "Widget UI controller source missing marker: $marker" }
}
foreach ($forbidden in @('DesktopWidgetStore', 'WritePrivateProfileStringW', 'ShellExecuteW(', 'WallpaperPackage::Validate')) {
    if ($widgetController.Contains($forbidden)) { throw "Widget UI controller regained domain ownership: $forbidden" }
}

foreach ($marker in @('DesktopWidgetUiAdapter', 'DesktopControlService.h', 'DesktopControlService service_')) {
    if (-not $widgetUiAdapterHeaderText.Contains($marker)) { throw "Legacy widget UI adapter header missing marker: $marker" }
}
foreach ($marker in @('service_.ListWidgets', 'service_.CreateWebWidget', 'service_.UpdateWidget', 'service_.RemoveWidget')) {
    if (-not $widgetUiAdapter.Contains($marker)) { throw "Legacy widget UI adapter is not routed through DesktopControlService: $marker" }
}
foreach ($forbidden in @('DesktopWidgetStore store', 'WritePrivateProfileStringW', 'ShellExecuteW(', 'WallpaperPackage::Validate')) {
    if ($widgetUiAdapter.Contains($forbidden)) { throw "Legacy widget UI adapter regained domain ownership: $forbidden" }
}
foreach ($marker in @('DesktopWidgetUiAdapter.h', '#define DesktopWidgetStore DesktopWidgetUiAdapter', '#include "WallpaperLibraryWindow.cpp"')) {
    if (-not $productionWindowText.Contains($marker)) { throw "Production WallpaperLibraryWindow bridge missing service-routing marker: $marker" }
}
if ($cmake.Contains('    src/WallpaperLibraryWindow.cpp')) {
    throw 'Production target must not compile legacy WallpaperLibraryWindow.cpp directly.'
}
if ($cmake.Contains('    src/WallpaperAutomationWindow.cpp')) {
    throw 'Production target must not compile legacy WallpaperAutomationWindow.cpp directly.'
}
foreach ($marker in @('src/DesktopWidgetUiAdapter.cpp', 'src/WallpaperLibraryWindowProduction.cpp', 'src/AutomationUiAdapter.cpp', 'src/WallpaperAutomationWindowProduction.cpp', 'src/PerformanceUiAdapter.cpp', 'src/WallpaperEngineProduction.cpp')) {
    if (-not $cmake.Contains($marker)) { throw "Production UI routing source missing from CMake: $marker" }
}

if (-not $adapter.Contains('DesktopControlService.h')) { throw 'Pi desktop tool adapter must include DesktopControlService.' }
if (-not $adapter.Contains('DesktopControlService service')) { throw 'Pi desktop tool adapter must delegate through DesktopControlService.' }
foreach ($forbidden in @('WritePrivateProfileStringW', 'DesktopWidgetStore store', 'ShellExecuteW(', 'WallpaperPackage::Validate')) {
    if ($adapter.Contains($forbidden)) { throw "Pi desktop tool adapter regained domain ownership: $forbidden" }
}

foreach ($sourceName in @('src/DesktopControlService.cpp', 'src/WallpaperService.cpp', 'src/WidgetService.cpp', 'src/WallpaperMonitorLayout.cpp')) {
    $count = ([regex]::Matches($cmake, [regex]::Escape($sourceName))).Count
    if ($count -lt 2) { throw "$sourceName must be linked into both TuringDesk and TuringDeskWallpaper." }
}
foreach ($wallpaperOnlySource in @('src/DesktopWidgetController.cpp', 'src/DesktopWidgetUiAdapter.cpp', 'src/AutomationService.cpp', 'src/AutomationUiAdapter.cpp', 'src/PerformanceService.cpp', 'src/PerformanceUiAdapter.cpp')) {
    if (([regex]::Matches($cmake, [regex]::Escape($wallpaperOnlySource))).Count -lt 1) {
        throw "$wallpaperOnlySource must be linked into TuringDeskWallpaper."
    }
}

foreach ($marker in @('UI / Pi / future Editor', 'Desktop Control contract', 'DesktopWidgetTools.cpp', 'WallpaperLibraryWindowV2.cpp')) {
    if (-not $doc.Contains($marker)) { throw "Desktop domain architecture doc missing marker: $marker" }
}

Write-Host 'Desktop domain contract OK.'
