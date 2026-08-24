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
$performanceHeader = Require-File 'src/native/include/turingdesk/PerformanceService.h'
$performanceSource = Require-File 'src/native/src/PerformanceService.cpp'
$widgetControllerHeader = Require-File 'src/native/include/turingdesk/DesktopWidgetController.h'
$widgetControllerSource = Require-File 'src/native/src/DesktopWidgetController.cpp'
$toolAdapter = Require-File 'src/native/src/DesktopWidgetTools.cpp'
$cmakePath = Require-File 'src/native/CMakeLists.txt'
$docPath = Require-File 'docs/DESKTOP_DOMAIN_ARCHITECTURE.md'

$header = Get-Content -LiteralPath $serviceHeader -Raw
$source = Get-Content -LiteralPath $serviceSource -Raw
$wallpaper = Get-Content -LiteralPath $wallpaperSource -Raw
$widget = Get-Content -LiteralPath $widgetSource -Raw
$performanceHeaderText = Get-Content -LiteralPath $performanceHeader -Raw
$performance = Get-Content -LiteralPath $performanceSource -Raw
$widgetControllerHeaderText = Get-Content -LiteralPath $widgetControllerHeader -Raw
$widgetController = Get-Content -LiteralPath $widgetControllerSource -Raw
$adapter = Get-Content -LiteralPath $toolAdapter -Raw
$cmake = Get-Content -LiteralPath $cmakePath -Raw
$doc = Get-Content -LiteralPath $docPath -Raw

foreach ($marker in @('DesktopControlService', 'GetState', 'ApplyWebPackage', 'CreateWebWidget', 'UpdateWidget', 'RemoveWidget', 'ListWidgets')) {
    if (-not $header.Contains($marker)) { throw "Desktop control header missing marker: $marker" }
}
foreach ($marker in @('WallpaperService.h', 'WidgetService.h')) {
    if (-not $header.Contains($marker)) { throw "Desktop facade missing domain dependency: $marker" }
}
foreach ($marker in @('DesktopControlService::GetState', 'DesktopControlService::ApplyWebPackage', 'DesktopControlService::CreateWebWidget')) {
    if (-not $source.Contains($marker)) { throw "Desktop control source missing marker: $marker" }
}
foreach ($forbidden in @('WritePrivateProfileStringW', 'DesktopWidgetStore store', 'WallpaperPackage::Validate')) {
    if ($source.Contains($forbidden)) { throw "Desktop facade regained domain persistence ownership: $forbidden" }
}

foreach ($marker in @('WallpaperService::GetState', 'WallpaperService::ApplyWebPackage', 'WritePrivateProfileStringW', 'WallpaperPackage::Validate')) {
    if (-not $wallpaper.Contains($marker)) { throw "Wallpaper domain service missing ownership marker: $marker" }
}
foreach ($marker in @('WidgetService::CreateWeb', 'WidgetService::Update', 'WidgetService::Remove', 'DesktopWidgetStore store')) {
    if (-not $widget.Contains($marker)) { throw "Widget domain service missing ownership marker: $marker" }
}
foreach ($marker in @('PerformanceService', 'WallpaperPerformancePolicy.h', 'GetConfig', 'SaveConfig')) {
    if (-not $performanceHeaderText.Contains($marker)) { throw "Performance service header missing marker: $marker" }
}
foreach ($marker in @('PerformanceService::GetConfig', 'PerformanceService::SaveConfig', 'WritePrivateProfileStringW', 'FullscreenAction', 'IdleThresholdSeconds')) {
    if (-not $performance.Contains($marker)) { throw "Performance domain service missing ownership marker: $marker" }
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

if (-not $adapter.Contains('DesktopControlService.h')) { throw 'Pi desktop tool adapter must include DesktopControlService.' }
if (-not $adapter.Contains('DesktopControlService service')) { throw 'Pi desktop tool adapter must delegate through DesktopControlService.' }
foreach ($forbidden in @('WritePrivateProfileStringW', 'DesktopWidgetStore store', 'ShellExecuteW(', 'WallpaperPackage::Validate')) {
    if ($adapter.Contains($forbidden)) { throw "Pi desktop tool adapter regained domain ownership: $forbidden" }
}

foreach ($sourceName in @('src/DesktopControlService.cpp', 'src/WallpaperService.cpp', 'src/WidgetService.cpp')) {
    $count = ([regex]::Matches($cmake, [regex]::Escape($sourceName))).Count
    if ($count -lt 2) { throw "$sourceName must be linked into both TuringDesk and TuringDeskWallpaper." }
}
foreach ($wallpaperOnlySource in @('src/DesktopWidgetController.cpp', 'src/AutomationService.cpp', 'src/PerformanceService.cpp')) {
    if (([regex]::Matches($cmake, [regex]::Escape($wallpaperOnlySource))).Count -lt 1) {
        throw "$wallpaperOnlySource must be linked into TuringDeskWallpaper."
    }
}

foreach ($marker in @('UI / Pi / future Editor', 'Desktop Control contract', 'DesktopWidgetTools.cpp', 'WallpaperLibraryWindowV2.cpp')) {
    if (-not $doc.Contains($marker)) { throw "Desktop domain architecture doc missing marker: $marker" }
}

Write-Host 'Desktop domain contract OK.'
