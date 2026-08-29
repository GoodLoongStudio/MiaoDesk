param(
    [string]$BuildDir = 'build/src/native/Release',
    [ValidateSet('baseline','settings','search','explorer','monitor')]
    [string]$Phase = 'baseline'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root (Join-Path $BuildDir 'MiaoDeskWidgetAcceptance.exe')
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
    throw "Missing M3 acceptance probe executable: $exe"
}

function Get-DiagnosticsDirectory {
    $base = if ($env:LOCALAPPDATA) { $env:LOCALAPPDATA } else { [IO.Path]::GetTempPath() }
    Join-Path (Join-Path $base 'MiaoDesk') 'Diagnostics'
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

function Initialize-ForegroundWindowInterop {
    if ('MiaoDesk.Acceptance.ForegroundWindowNative' -as [type]) { return }
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
namespace MiaoDesk.Acceptance {
    public static class ForegroundWindowNative {
        [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
        [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint processId);
        [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowText(IntPtr hWnd, StringBuilder text, int maxCount);
        [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassName(IntPtr hWnd, StringBuilder className, int maxCount);
    }
}
'@
}

function Get-ForegroundWindowObservation {
    Initialize-ForegroundWindowInterop
    $hwnd = [MiaoDesk.Acceptance.ForegroundWindowNative]::GetForegroundWindow()
    if ($hwnd -eq [IntPtr]::Zero) { return $null }

    [uint32]$processId = 0
    $null = [MiaoDesk.Acceptance.ForegroundWindowNative]::GetWindowThreadProcessId($hwnd, [ref]$processId)
    if ($processId -eq 0) { return $null }

    $process = Get-Process -Id $processId -ErrorAction SilentlyContinue
    if ($null -eq $process) { return $null }

    $className = New-Object Text.StringBuilder 256
    $title = New-Object Text.StringBuilder 1024
    $null = [MiaoDesk.Acceptance.ForegroundWindowNative]::GetClassName($hwnd, $className, $className.Capacity)
    $null = [MiaoDesk.Acceptance.ForegroundWindowNative]::GetWindowText($hwnd, $title, $title.Capacity)

    [pscustomobject]@{
        capturedAtUtc = [DateTime]::UtcNow.ToString('o')
        processName = $process.ProcessName
        processId = [int]$processId
        sessionId = $process.SessionId
        className = $className.ToString()
        title = $title.ToString()
    }
}

function Wait-ForExpectedMiaoDeskWindowEvidence([string]$AcceptancePhase, [string]$DiagnosticsDir) {
    if ($AcceptancePhase -notin @('settings','search')) { return }

    $expectedClass = if ($AcceptancePhase -eq 'settings') { 'MiaoDesk.Native.DesktopLibrary' } else { 'MiaoDesk.Native.SearchWindow' }
    $expectedProcess = if ($AcceptancePhase -eq 'settings') { 'MiaoDeskWallpaper' } else { 'MiaoDesk' }
    $deadline = [DateTime]::UtcNow.AddSeconds(60)
    Write-Host "Waiting for foreground MiaoDesk $AcceptancePhase window evidence: class=$expectedClass process=$expectedProcess"
    Write-Host 'Keep the required product window foreground while this phase observes it; the health probe runs immediately afterwards.'

    while ([DateTime]::UtcNow -lt $deadline) {
        $observation = Get-ForegroundWindowObservation
        if ($null -ne $observation -and
            $observation.className -eq $expectedClass -and
            $observation.processName -eq $expectedProcess -and
            $observation.sessionId -eq [System.Diagnostics.Process]::GetCurrentProcess().SessionId) {
            $path = Join-Path $DiagnosticsDir "widget-acceptance-$AcceptancePhase.window.json"
            $evidence = [ordered]@{
                schema = 'miaodesk.widget-window-evidence.v1'
                phase = $AcceptancePhase
                capturedAtUtc = $observation.capturedAtUtc
                processName = $observation.processName
                processId = $observation.processId
                sessionId = $observation.sessionId
                className = $observation.className
                title = $observation.title
            }
            $evidence | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $path -Encoding utf8
            Write-Host "Observed required MiaoDesk $AcceptancePhase foreground window: $($observation.className) pid=$($observation.processId)"
            return
        }
        Start-Sleep -Milliseconds 250
    }

    throw "M3 $AcceptancePhase acceptance is unproven: the expected foreground product window was not observed within 60 seconds (class=$expectedClass process=$expectedProcess)."
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

function Get-AcceptanceBinaryFingerprint([string]$Path) {
    $item = Get-Item -LiteralPath $Path -ErrorAction Stop
    [ordered]@{
        sha256 = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
        length = [int64]$item.Length
        path = $item.FullName
    }
}

function Read-AcceptanceBinaryCheckpoint([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw 'Acceptance binary checkpoint is missing. Start a new phase=baseline with the exact executable that will be used for the full M3 sequence.'
    }
    $values = @{}
    foreach ($line in Get-Content -LiteralPath $Path -ErrorAction Stop) {
        $parts = $line.Trim().Split('=', 2)
        if ($parts.Count -eq 2) { $values[$parts[0]] = $parts[1] }
    }
    if (-not $values.ContainsKey('sha256') -or -not $values.ContainsKey('length')) {
        throw 'Acceptance binary checkpoint is malformed. Start a new baseline.'
    }
    $values
}

function Assert-AcceptanceBinaryContinuity([string]$Path, [string]$ExecutablePath) {
    $expected = Read-AcceptanceBinaryCheckpoint -Path $Path
    $current = Get-AcceptanceBinaryFingerprint -Path $ExecutablePath
    if (([string]$expected['sha256']).ToLowerInvariant() -ne $current.sha256 -or [int64]$expected['length'] -ne $current.length) {
        throw "M3 acceptance binary changed after baseline. expectedSha256=$($expected['sha256']) currentSha256=$($current.sha256) expectedLength=$($expected['length']) currentLength=$($current.length). Start a new baseline; evidence from different builds cannot be combined."
    }
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

function Capture-DesktopVisualEvidence([string]$DiagnosticsDir, [string]$AcceptancePhase) {
    Add-Type -AssemblyName System.Windows.Forms
    Add-Type -AssemblyName System.Drawing

    $screens = @([System.Windows.Forms.Screen]::AllScreens)
    if ($screens.Count -eq 0) {
        throw 'Visual acceptance evidence could not be captured because no Windows display is available.'
    }

    $left = ($screens | ForEach-Object { $_.Bounds.Left } | Measure-Object -Minimum).Minimum
    $top = ($screens | ForEach-Object { $_.Bounds.Top } | Measure-Object -Minimum).Minimum
    $right = ($screens | ForEach-Object { $_.Bounds.Right } | Measure-Object -Maximum).Maximum
    $bottom = ($screens | ForEach-Object { $_.Bounds.Bottom } | Measure-Object -Maximum).Maximum
    $width = [int]($right - $left)
    $height = [int]($bottom - $top)
    if ($width -le 0 -or $height -le 0) {
        throw "Visual acceptance evidence has invalid virtual desktop bounds: $left,$top,$right,$bottom"
    }

    New-Item -ItemType Directory -Path $DiagnosticsDir -Force | Out-Null
    $pngPath = Join-Path $DiagnosticsDir "widget-acceptance-$AcceptancePhase.png"
    $hashPath = Join-Path $DiagnosticsDir "widget-acceptance-$AcceptancePhase.png.sha256"
    $bitmap = New-Object System.Drawing.Bitmap $width, $height
    try {
        $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
        try {
            $graphics.CopyFromScreen($left, $top, 0, 0, (New-Object System.Drawing.Size $width, $height))
        }
        finally {
            $graphics.Dispose()
        }
        $bitmap.Save($pngPath, [System.Drawing.Imaging.ImageFormat]::Png)
    }
    finally {
        $bitmap.Dispose()
    }

    $hash = (Get-FileHash -LiteralPath $pngPath -Algorithm SHA256).Hash.ToLowerInvariant()
    Write-CheckpointLines -Path $hashPath -Lines @(
        "sha256=$hash",
        "capturedAtUtc=$([DateTime]::UtcNow.ToString('o'))",
        "phase=$AcceptancePhase",
        "virtualBounds=$left,$top,$width,$height"
    )
    Write-Host "Captured M3 visual evidence: $pngPath (sha256=$hash)"
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
$binaryCheckpoint = Join-Path $diagnostics 'widget-acceptance-binary.sha256'
$configCheckpoint = Join-Path $diagnostics 'widget-acceptance-baseline.config'
$sealedManifest = Join-Path $diagnostics 'widget-acceptance-evidence.manifest.json'
$sealedManifestHash = Join-Path $diagnostics 'widget-acceptance-evidence.manifest.sha256'

if ($Phase -eq 'baseline') {
    foreach ($checkpoint in @($explorerCheckpoint, $monitorCheckpoint, $monitorEvidence, $binaryCheckpoint, $configCheckpoint, $sealedManifest, $sealedManifestHash)) {
        if (Test-Path -LiteralPath $checkpoint -PathType Leaf) {
            Remove-Item -LiteralPath $checkpoint -Force
        }
    }
    Get-ChildItem -LiteralPath $diagnostics -Filter 'widget-acceptance-*.png*' -ErrorAction SilentlyContinue |
        Remove-Item -Force -ErrorAction SilentlyContinue
    Get-ChildItem -LiteralPath $diagnostics -Filter 'widget-acceptance-*.txt' -ErrorAction SilentlyContinue |
        Remove-Item -Force -ErrorAction SilentlyContinue
    Get-ChildItem -LiteralPath $diagnostics -Filter 'widget-acceptance-*.window.json' -ErrorAction SilentlyContinue |
        Remove-Item -Force -ErrorAction SilentlyContinue

    $binary = Get-AcceptanceBinaryFingerprint -Path $exe
    Write-CheckpointLines -Path $binaryCheckpoint -Lines @(
        "sha256=$($binary.sha256)",
        "length=$($binary.length)",
        "capturedAtUtc=$([DateTime]::UtcNow.ToString('o'))",
        "fileName=MiaoDeskWidgetAcceptance.exe"
    )
    Write-Host "Captured M3 acceptance binary checkpoint: sha256=$($binary.sha256) length=$($binary.length)"
}
else {
    Assert-AcceptanceBinaryContinuity -Path $binaryCheckpoint -ExecutablePath $exe
}

if ($Phase -in @('settings','search')) {
    Wait-ForExpectedMiaoDeskWindowEvidence -AcceptancePhase $Phase -DiagnosticsDir $diagnostics
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
    Write-Host 'This phase must match the baseline Widget identity and placement configuration, use the same acceptance binary, and follow baseline -> settings -> search -> explorer -> monitor.'
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
    Capture-DesktopVisualEvidence -DiagnosticsDir $diagnostics -AcceptancePhase $Phase
    Write-Host 'Widget acceptance probe passed for this phase, captured visual evidence, and advanced the sequence cursor.'
    exit 0
}

$meaning = switch ($code) {
    60 { 'interactive Windows desktop unavailable' }
    61 { 'no enabled Web Widget' }
    62 { 'Widget runtime health unavailable' }
    63 { 'one or more Widget surfaces are unhealthy' }
    64 { 'acceptance report/sequence/config write failed' }
    65 { 'baseline identity/config set missing; run the baseline phase first' }
    66 { 'enabled Widget identity or placement configuration changed since baseline' }
    67 { 'acceptance phase is out of order; run baseline -> settings -> search -> explorer -> monitor without skipping a successful phase' }
    default { "unexpected probe exit code $code" }
}
throw "Widget acceptance probe failed: $meaning. Read %LOCALAPPDATA%\MiaoDesk\Diagnostics\widget-acceptance-$Phase.txt when present."
