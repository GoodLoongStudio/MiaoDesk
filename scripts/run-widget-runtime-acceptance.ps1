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

function Get-CurrentDisplayTopology {
    Add-Type -AssemblyName System.Windows.Forms
    @(
        [System.Windows.Forms.Screen]::AllScreens |
            ForEach-Object {
                $bounds = $_.Bounds
                '{0}|{1},{2},{3},{4}|primary={5}' -f $_.DeviceName, $bounds.X, $bounds.Y, $bounds.Width, $bounds.Height, $_.Primary
            } |
            Sort-Object -Unique
    )
}

function Read-CheckpointLines([string]$Path, [string]$Description) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Description evidence is missing."
    }
    @(
        Get-Content -LiteralPath $Path -ErrorAction Stop |
            ForEach-Object { $_.Trim() } |
            Where-Object { $_ } |
            Sort-Object -Unique
    )
}

function Write-CheckpointLines([string]$Path, [string[]]$Lines) {
    New-Item -ItemType Directory -Path (Split-Path -Parent $Path) -Force | Out-Null
    Set-Content -LiteralPath $Path -Value $Lines -Encoding utf8
}

function Write-MonitorTransitionEvidence([string]$Path, [string[]]$Before, [string[]]$Changed, [string[]]$Stable) {
    $lines = @(
        "observedAtUtc=$([DateTime]::UtcNow.ToString('o'))",
        "before=$($Before -join '; ')"
        "changed=$($Changed -join '; ')"
        "stable=$($Stable -join '; ')"
    )
    Write-CheckpointLines -Path $Path -Lines $lines
}

function Wait-ForMonitorTopologyTransition([string[]]$Before, [string]$EvidencePath) {
    $beforeKey = $Before -join "`n"
    $changeDeadline = [DateTime]::UtcNow.AddMinutes(3)
    $changed = $null

    Write-Host 'Waiting for an observed display topology change. Disconnect/reconnect a display or change the Windows display topology while this phase is running.'
    while ([DateTime]::UtcNow -lt $changeDeadline) {
        $current = @(Get-CurrentDisplayTopology)
        if ($current.Count -gt 0 -and (($current -join "`n") -ne $beforeKey)) {
            $changed = $current
            break
        }
        Start-Sleep -Milliseconds 750
    }
    if ($null -eq $changed) {
        throw 'Monitor recovery is unproven: no display topology change was observed during the monitor phase.'
    }

    $stableDeadline = [DateTime]::UtcNow.AddSeconds(30)
    $stableKey = $changed -join "`n"
    $stableSamples = 0
    $last = $changed
    while ([DateTime]::UtcNow -lt $stableDeadline) {
        $current = @(Get-CurrentDisplayTopology)
        $currentKey = $current -join "`n"
        if ($current.Count -gt 0 -and $currentKey -eq $stableKey) {
            $stableSamples++
            if ($stableSamples -ge 4) {
                Write-MonitorTransitionEvidence -Path $EvidencePath -Before $Before -Changed $changed -Stable $current
                Write-Host "Observed stable display topology after change: $($current -join '; ')"
                return $current
            }
        }
        else {
            $stableKey = $currentKey
            $stableSamples = 1
            $last = $current
        }
        Start-Sleep -Milliseconds 750
    }

    throw "Monitor recovery is unproven: display topology changed but did not stabilize. Last topology: $($last -join '; ')"
}

$diagnostics = Get-DiagnosticsDirectory
$explorerCheckpoint = Join-Path $diagnostics 'widget-acceptance-search.explorer-pids'
$monitorCheckpoint = Join-Path $diagnostics 'widget-acceptance-explorer.monitor-topology'
$monitorEvidence = Join-Path $diagnostics 'widget-acceptance-monitor.topology-transition'

if ($Phase -eq 'baseline') {
    foreach ($checkpoint in @($explorerCheckpoint, $monitorCheckpoint, $monitorEvidence)) {
        if (Test-Path -LiteralPath $checkpoint -PathType Leaf) {
            Remove-Item -LiteralPath $checkpoint -Force
        }
    }
}

if ($Phase -eq 'explorer') {
    $before = @(Read-CheckpointLines -Path $explorerCheckpoint -Description 'Explorer recovery')
    $after = @(Get-CurrentSessionExplorerPids | ForEach-Object { [string]$_ })
    if ($before.Count -eq 0 -or $after.Count -eq 0) {
        throw 'Explorer recovery evidence is unavailable for the current interactive session.'
    }
    if (($before -join ',') -eq ($after -join ',')) {
        throw "Explorer recovery is unproven: current-session explorer.exe PID set did not change ($($after -join ',')). Restart Explorer after the successful search phase, wait for the desktop to return, then run phase=explorer."
    }
    Write-Host "Explorer restart evidence matched: before=$($before -join ',') after=$($after -join ',')"
}

if ($Phase -eq 'monitor') {
    $beforeTopology = @(Read-CheckpointLines -Path $monitorCheckpoint -Description 'Monitor topology')
    if ($beforeTopology.Count -eq 0) {
        throw 'Monitor recovery evidence is unavailable. Run a successful explorer phase before the monitor phase.'
    }
    if (Test-Path -LiteralPath $monitorEvidence -PathType Leaf) {
        Remove-Item -LiteralPath $monitorEvidence -Force
    }
    $null = Wait-ForMonitorTopologyTransition -Before $beforeTopology -EvidencePath $monitorEvidence
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
        Write-CheckpointLines -Path $explorerCheckpoint -Lines ($pids | ForEach-Object { [string]$_ })
        Write-Host "Captured Explorer recovery checkpoint: $($pids -join ',')"
    }
    if ($Phase -eq 'explorer') {
        $topology = @(Get-CurrentDisplayTopology)
        if ($topology.Count -eq 0) {
            throw 'Explorer phase passed Widget health, but no display topology could be captured for monitor recovery evidence.'
        }
        Write-CheckpointLines -Path $monitorCheckpoint -Lines $topology
        Write-Host "Captured monitor recovery checkpoint: $($topology -join '; ')"
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
