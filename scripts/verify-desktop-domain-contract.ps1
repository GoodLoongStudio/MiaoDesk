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
$toolAdapter = Require-File 'src/native/src/DesktopWidgetTools.cpp'
$cmakePath = Require-File 'src/native/CMakeLists.txt'
$docPath = Require-File 'docs/DESKTOP_DOMAIN_ARCHITECTURE.md'

$header = Get-Content -LiteralPath $serviceHeader -Raw
$source = Get-Content -LiteralPath $serviceSource -Raw
$adapter = Get-Content -LiteralPath $toolAdapter -Raw
$cmake = Get-Content -LiteralPath $cmakePath -Raw
$doc = Get-Content -LiteralPath $docPath -Raw

foreach ($marker in @('DesktopControlService', 'GetState', 'ApplyWebPackage', 'CreateWebWidget', 'UpdateWidget', 'RemoveWidget', 'ListWidgets')) {
    if (-not $header.Contains($marker)) { throw "Desktop control header missing marker: $marker" }
}

foreach ($marker in @('DesktopControlService::GetState', 'DesktopControlService::ApplyWebPackage', 'DesktopControlService::CreateWebWidget')) {
    if (-not $source.Contains($marker)) { throw "Desktop control source missing marker: $marker" }
}

if (-not $adapter.Contains('DesktopControlService.h')) { throw 'Pi desktop tool adapter must include DesktopControlService.' }
if (-not $adapter.Contains('DesktopControlService service')) { throw 'Pi desktop tool adapter must delegate through DesktopControlService.' }

foreach ($forbidden in @('WritePrivateProfileStringW', 'DesktopWidgetStore store', 'ShellExecuteW(', 'WallpaperPackage::Validate')) {
    if ($adapter.Contains($forbidden)) { throw "Pi desktop tool adapter regained domain ownership: $forbidden" }
}

$serviceOccurrences = ([regex]::Matches($cmake, 'src/DesktopControlService\.cpp')).Count
if ($serviceOccurrences -lt 2) { throw 'DesktopControlService must be linked into both TuringDesk and TuringDeskWallpaper.' }

foreach ($marker in @('UI / Pi / future Editor', 'Desktop Control contract', 'DesktopWidgetTools.cpp', 'WallpaperLibraryWindowV2.cpp')) {
    if (-not $doc.Contains($marker)) { throw "Desktop domain architecture doc missing marker: $marker" }
}

Write-Host 'Desktop domain contract OK.'
