param()

$ErrorActionPreference = 'Stop'

function Get-DiagnosticsDirectory {
    $base = if ($env:LOCALAPPDATA) { $env:LOCALAPPDATA } else { [IO.Path]::GetTempPath() }
    Join-Path (Join-Path $base 'TuringDesk') 'Diagnostics'
}

function Get-RequiredEvidencePaths([string]$DiagnosticsDir) {
    $paths = @(
        'widget-acceptance-baseline.ids',
        'widget-acceptance-sequence.phase',
        'widget-acceptance-binary.sha256',
        'widget-acceptance-search.explorer-pids',
        'widget-acceptance-explorer.monitor-topology',
        'widget-acceptance-monitor.topology-transition'
    )
    foreach ($phase in @('baseline','settings','search','explorer','monitor')) {
        $paths += "widget-acceptance-$phase.txt"
        $paths += "widget-acceptance-$phase.png"
        $paths += "widget-acceptance-$phase.png.sha256"
    }
    @($paths | ForEach-Object { Join-Path $DiagnosticsDir $_ })
}

function Get-FileEvidence([string]$Path, [string]$DiagnosticsDir) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "M3 acceptance evidence is incomplete: missing $Path"
    }
    $item = Get-Item -LiteralPath $Path
    [ordered]@{
        path = $item.Name
        length = [int64]$item.Length
        sha256 = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
        lastWriteUtc = $item.LastWriteTimeUtc.ToString('o')
    }
}

function Read-KeyValueFile([string]$Path) {
    $map = @{}
    foreach ($line in Get-Content -LiteralPath $Path -ErrorAction Stop) {
        $parts = $line.Trim().Split('=', 2)
        if ($parts.Count -eq 2) { $map[$parts[0]] = $parts[1] }
    }
    $map
}

$diagnostics = Get-DiagnosticsDirectory
if (-not (Test-Path -LiteralPath $diagnostics -PathType Container)) {
    throw "M3 acceptance diagnostics directory is missing: $diagnostics"
}

$sequencePath = Join-Path $diagnostics 'widget-acceptance-sequence.phase'
if (-not (Test-Path -LiteralPath $sequencePath -PathType Leaf)) {
    throw 'M3 acceptance sequence cursor is missing; run the full baseline -> settings -> search -> explorer -> monitor flow first.'
}
$sequence = (Get-Content -LiteralPath $sequencePath -Raw).Trim()
if ($sequence -ne 'monitor') {
    throw "M3 acceptance evidence cannot be sealed before the monitor phase succeeds. Current sequence cursor: '$sequence'"
}

$binaryCheckpointPath = Join-Path $diagnostics 'widget-acceptance-binary.sha256'
$binaryCheckpoint = Read-KeyValueFile -Path $binaryCheckpointPath
if (-not $binaryCheckpoint.ContainsKey('sha256') -or -not $binaryCheckpoint.ContainsKey('length')) {
    throw 'M3 acceptance binary checkpoint is incomplete; start a new baseline with the intended TuringDeskWidgetAcceptance.exe.'
}

$files = @()
foreach ($path in @(Get-RequiredEvidencePaths -DiagnosticsDir $diagnostics)) {
    $files += Get-FileEvidence -Path $path -DiagnosticsDir $diagnostics
}

$baselineIdsPath = Join-Path $diagnostics 'widget-acceptance-baseline.ids'
$baselineIds = @(
    Get-Content -LiteralPath $baselineIdsPath |
        ForEach-Object { $_.Trim() } |
        Where-Object { $_ } |
        Sort-Object -Unique
)
if ($baselineIds.Count -eq 0) {
    throw 'M3 acceptance baseline identity set is empty; evidence cannot be sealed.'
}

$manifest = [ordered]@{
    schema = 'turingdesk.widget-acceptance-evidence.v1'
    sealedAtUtc = [DateTime]::UtcNow.ToString('o')
    sequence = $sequence
    acceptanceBinary = [ordered]@{
        fileName = 'TuringDeskWidgetAcceptance.exe'
        sha256 = ([string]$binaryCheckpoint['sha256']).ToLowerInvariant()
        length = [int64]$binaryCheckpoint['length']
    }
    baselineWidgetIds = $baselineIds
    machine = [ordered]@{
        computerName = $env:COMPUTERNAME
        userDomain = $env:USERDOMAIN
        sessionId = [System.Diagnostics.Process]::GetCurrentProcess().SessionId
        osVersion = [Environment]::OSVersion.VersionString
        processArchitecture = [System.Runtime.InteropServices.RuntimeInformation]::ProcessArchitecture.ToString()
    }
    files = $files
}

$json = $manifest | ConvertTo-Json -Depth 6
$manifestPath = Join-Path $diagnostics 'widget-acceptance-evidence.manifest.json'
Set-Content -LiteralPath $manifestPath -Value $json -Encoding utf8
$manifestHash = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash.ToLowerInvariant()
$sealPath = Join-Path $diagnostics 'widget-acceptance-evidence.manifest.sha256'
Set-Content -LiteralPath $sealPath -Value @(
    "sha256=$manifestHash",
    "schema=turingdesk.widget-acceptance-evidence.v1",
    "sealedAtUtc=$([DateTime]::UtcNow.ToString('o'))"
) -Encoding utf8

$verifier = Join-Path $PSScriptRoot 'verify-widget-acceptance-evidence.ps1'
if (-not (Test-Path -LiteralPath $verifier -PathType Leaf)) {
    throw "M3 acceptance evidence verifier is missing: $verifier"
}
& $verifier
if ($LASTEXITCODE -ne 0) {
    throw "M3 acceptance evidence verifier failed after sealing with exit code $LASTEXITCODE"
}

Write-Host "Sealed and verified M3 Widget acceptance evidence: $manifestPath"
Write-Host "Acceptance binary SHA-256: $($manifest.acceptanceBinary.sha256)"
Write-Host "Manifest SHA-256: $manifestHash"
