param(
    [string]$BuildDir = 'build/src/native/Release',
    [ValidateSet('baseline','settings','search','explorer','monitor')]
    [string]$Phase = 'baseline'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root (Join-Path $BuildDir 'TuringDeskWidgetAcceptance.exe')
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
    throw "Missing M3 acceptance probe executable: $exe"
}

Write-Host "Running real-Windows Widget acceptance probe: phase=$Phase"
if ($Phase -ne 'baseline') {
    Write-Host 'This phase must match the baseline Widget identity set and follow baseline -> settings -> search -> explorer -> monitor.'
}
& $exe "--phase=$Phase"
$code = $LASTEXITCODE
if ($code -eq 0) {
    Write-Host 'Widget acceptance probe passed for this phase and advanced the sequence cursor.'
    exit 0
}

$meaning = switch ($code) {
    60 { 'interactive Windows desktop unavailable' }
    61 { 'no enabled Web Widget' }
    62 { 'Widget runtime health unavailable' }
    63 { 'one or more Widget surfaces are unhealthy' }
    64 { 'acceptance report/sequence write failed' }
    65 { 'baseline identity set missing; run the baseline phase first' }
    66 { 'enabled Widget identity set changed since baseline' }
    67 { 'acceptance phase is out of order; run baseline -> settings -> search -> explorer -> monitor without skipping a successful phase' }
    default { "unexpected probe exit code $code" }
}
throw "Widget acceptance probe failed: $meaning. Read %LOCALAPPDATA%\TuringDesk\Diagnostics\widget-acceptance-$Phase.txt when present."
