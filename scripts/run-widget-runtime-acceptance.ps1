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

function Get-DiagnosticsDirectory {
    $base = if ($env:LOCALAPPDATA) { $env:LOCALAPPDATA } else { [IO.Path]::GetTempPath() }
    Join-Path (Join-Path $base 'TuringDesk') 'Diagnostics'
}

function Get-CurrentSessionExplorerPids {
    $sessionId = [System.Diagnostics.Process]::GetCurrentProcess().SessionId
    @(
        Get-Process -Name explorer -ErrorAction SilentlyContinue |
            Where-Object { $_.SessionId -eq $sessionId } |
            Select-Object -ExpandProperty Id |
            Sort-Object -Unique
    )
}

$diagnostics = Get-DiagnosticsDirectory
$explorerCheckpoint = Join-Path $diagnostics 'widget-acceptance-search.explorer-pids'

if ($Phase -eq 'baseline' -and (Test-Path -LiteralPath $explorerCheckpoint -PathType Leaf)) {
    Remove-Item -LiteralPath $explorerCheckpoint -Force
}

if ($Phase -eq 'explorer') {
    if (-not (Test-Path -LiteralPath $explorerCheckpoint -PathType Leaf)) {
        throw 'Explorer recovery evidence is missing. Run a successful search phase immediately before restarting Explorer.'
    }
    $before = @(
        Get-Content -LiteralPath $explorerCheckpoint -ErrorAction Stop |
            ForEach-Object { $_.Trim() } |
            Where-Object { $_ } |
            Sort-Object -Unique
    )
    $after = @(Get-CurrentSessionExplorerPids | ForEach-Object { [string]$_ })
    if ($before.Count -eq 0 -or $after.Count -eq 0) {
        throw 'Explorer recovery evidence is unavailable for the current interactive session.'
    }
    if (($before -join ',') -eq ($after -join ',')) {
        throw "Explorer recovery is unproven: current-session explorer.exe PID set did not change ($($after -join ',')). Restart Explorer after the successful search phase, wait for the desktop to return, then run phase=explorer."
    }
    Write-Host "Explorer restart evidence matched: before=$($before -join ',') after=$($after -join ',')"
}

Write-Host "Running real-Windows Widget acceptance probe: phase=$Phase"
if ($Phase -ne 'baseline') {
    Write-Host 'This phase must match the baseline Widget identity set and follow baseline -> settings -> search -> explorer -> monitor.'
}
& $exe "--phase=$Phase"
$code = $LASTEXITCODE
if ($code -eq 0) {
    if ($Phase -eq 'search') {
        $pids = @(Get-CurrentSessionExplorerPids)
        if ($pids.Count -eq 0) {
            throw 'Search phase passed Widget health, but no current-session explorer.exe PID could be captured for the Explorer recovery checkpoint.'
        }
        New-Item -ItemType Directory -Path $diagnostics -Force | Out-Null
        Set-Content -LiteralPath $explorerCheckpoint -Value ($pids | ForEach-Object { [string]$_ }) -Encoding ascii
        Write-Host "Captured Explorer recovery checkpoint: $($pids -join ',')"
    }
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
