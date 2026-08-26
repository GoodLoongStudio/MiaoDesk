param(
    [ValidateSet('baseline','settings','search','explorer','monitor')]
    [string]$Phase = 'baseline'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$runner = Join-Path $PSScriptRoot 'run-widget-runtime-acceptance.ps1'
$localAppData = if ($env:LOCALAPPDATA) { $env:LOCALAPPDATA } else { throw 'LOCALAPPDATA is unavailable.' }
$installedDir = Join-Path $localAppData 'TuringDesk\NativeTest'
$probe = Join-Path $installedDir 'TuringDeskWidgetAcceptance.exe'
$installedMarker = Join-Path $installedDir '.installed-build-sha'

if (-not (Test-Path -LiteralPath $runner -PathType Leaf)) {
    throw "M3 acceptance phase runner is missing: $runner"
}
if (-not (Test-Path -LiteralPath $probe -PathType Leaf)) {
    throw "Installed M3 acceptance probe is missing: $probe. Run the ARM64 one-click updater for a build that packages TuringDeskWidgetAcceptance.exe."
}

$buildSha = ''
if (Test-Path -LiteralPath $installedMarker -PathType Leaf) {
    $buildSha = ([string](Get-Content -LiteralPath $installedMarker -Raw -ErrorAction SilentlyContinue)).Trim()
}

Write-Host "Running installed ARM64 M3 Widget acceptance: phase=$Phase" -ForegroundColor Cyan
Write-Host "Probe: $probe" -ForegroundColor DarkGray
if ($buildSha) { Write-Host "Installed validated build: $buildSha" -ForegroundColor DarkGray }

& $runner -BuildDir $installedDir -Phase $Phase
exit $LASTEXITCODE
