param()

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

$probe = Join-Path $root 'src/native/src/desktop/widgets/WidgetRuntimeAcceptance.cpp'
$main = Join-Path $root 'src/native/src/desktop/widgets/WidgetRuntimeAcceptanceMain.cpp'
$header = Join-Path $root 'src/native/include/turingdesk/WidgetRuntimeAcceptance.h'
$runner = Join-Path $root 'scripts/run-widget-runtime-acceptance.ps1'
$cmake = Join-Path $root 'src/native/CMakeLists.txt'
$doc = Join-Path $root 'docs/WIDGET_RUNTIME_HEALTH_M3.md'

foreach ($path in @($probe, $main, $header, $runner, $cmake, $doc)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing M3 Widget acceptance contract input: $path" }
}

$probeText = Get-Content -LiteralPath $probe -Raw
foreach ($marker in @('WidgetService', 'GetRuntimeHealth', 'InteractiveDesktopAvailable', 'renderingHealthy', 'widget-acceptance-')) {
    if (-not $probeText.Contains($marker)) { throw "Widget acceptance probe missing marker: $marker" }
}
foreach ($forbidden in @('FindWindowW(', 'FindWindowExW(', 'EnumWindows(', 'SetParent(', 'SetWindowPos(', 'GetPrivateProfileStringW')) {
    if ($probeText.Contains($forbidden)) { throw "Widget acceptance probe bypasses Widget/DesktopShell domain ownership: $forbidden" }
}

$mainText = Get-Content -LiteralPath $main -Raw
if (-not $mainText.Contains('RunWidgetRuntimeAcceptanceProbe') -or -not $mainText.Contains('--phase=')) {
    throw 'Widget acceptance executable must delegate to the Widget-domain probe and preserve phase labels.'
}

$cmakeText = Get-Content -LiteralPath $cmake -Raw
foreach ($marker in @('TuringDeskWidgetAcceptance', 'WidgetRuntimeAcceptance.cpp', 'WidgetRuntimeAcceptanceMain.cpp')) {
    if (-not $cmakeText.Contains($marker)) { throw "M3 acceptance probe missing from build graph: $marker" }
}

$docText = Get-Content -LiteralPath $doc -Raw
foreach ($marker in @('TuringDeskWidgetAcceptance.exe', 'baseline', 'settings', 'search', 'explorer', 'monitor', 'non-interactive CI')) {
    if (-not $docText.Contains($marker)) { throw "M3 acceptance documentation missing marker: $marker" }
}

Write-Host 'M3 Widget acceptance contract OK: real-Windows probe consumes WidgetService health without regaining HWND/shell ownership.'
