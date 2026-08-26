$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$updaterPath = Join-Path $root 'scripts\update-turingdesk-arm64.ps1'
if (-not (Test-Path -LiteralPath $updaterPath -PathType Leaf)) {
    throw 'ARM64 updater script is missing.'
}

$text = Get-Content -LiteralPath $updaterPath -Raw

$required = @(
    '$RuntimeStoreDir = Join-Path $DeployParent "RuntimeBundle"',
    'function New-Junction',
    'New-Item -ItemType Junction',
    'function Link-RuntimeBundle',
    'function Try-LinkUnchangedRuntimeBundle',
    'function Promote-ReusedRuntimeBundle',
    'Move-Item -LiteralPath $source',
    'Link-RuntimeBundle $StagedRoot $RuntimeStoreDir',
    'Materializing changed TuringDesk RuntimeBundle'
)

foreach ($marker in $required) {
    if (-not $text.Contains($marker)) {
        throw "ARM64 updater runtime-reuse contract is missing marker: $marker"
    }
}

$forbidden = @(
    'Copy-TreeLongPath',
    'robocopy.exe',
    'Copy-UnchangedRuntimeBundle'
)
foreach ($marker in $forbidden) {
    if ($text.Contains($marker)) {
        throw "ARM64 updater must not deep-copy an unchanged RuntimeBundle: $marker"
    }
}

if ($text -match 'Copy-Item[^\r\n]+\$DeployDir[^\r\n]+(?:Runtime|Pi|Goz)') {
    throw 'ARM64 updater must not recursively copy Runtime/Pi/Goz from the installed package.'
}

if ($text -match 'Copy-Item[^\r\n]+RuntimeStoreDir') {
    throw 'ARM64 updater must link the shared RuntimeBundle instead of copying it into staging.'
}

if ($text -notmatch 'if \(-not \$reusedRuntime\) \{ Materialize-Runtime') {
    throw 'ARM64 updater must materialize the RuntimeBundle only when the persisted bundle changed.'
}

Write-Host 'ARM64 updater shared RuntimeBundle contract verified.' -ForegroundColor Green
