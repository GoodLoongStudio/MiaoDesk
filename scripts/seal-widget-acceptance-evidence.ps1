param()

$ErrorActionPreference = 'Stop'

function Get-DiagnosticsDirectory {
    $base = if ($env:LOCALAPPDATA) { $env:LOCALAPPDATA } else { [IO.Path]::GetTempPath() }
    Join-Path (Join-Path $base 'MiaoDesk') 'Diagnostics'
}

function Get-RequiredEvidencePaths([string]$DiagnosticsDir) {
    $paths = @(
        'widget-acceptance-baseline.ids',
        'widget-acceptance-baseline.session',
        'widget-acceptance-baseline.config',
        'widget-acceptance-sequence.phase',
        'widget-acceptance-binary.sha256',
        'widget-acceptance-settings.window.json',
        'widget-acceptance-search.window.json',
        'widget-acceptance-search.explorer-pids',
        'widget-acceptance-explorer.monitor-topology',
        'widget-acceptance-monitor.topology-transition',
        'widget-acceptance-human-visual.json',
        'widget-acceptance-human-visual.json.sha256'
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

function Read-BaselineSessionId([string]$DiagnosticsDir) {
    $path = Join-Path $DiagnosticsDir 'widget-acceptance-baseline.session'
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw 'M3 baseline Windows session checkpoint is missing; start a new baseline in the interactive session used for the full sequence.'
    }
    $value = (Get-Content -LiteralPath $path -Raw -ErrorAction Stop).Trim()
    $sessionId = 0
    if (-not [int]::TryParse($value, [ref]$sessionId) -or $sessionId -lt 0) {
        throw "M3 baseline Windows session checkpoint is malformed: '$value'"
    }
    $currentSessionId = [System.Diagnostics.Process]::GetCurrentProcess().SessionId
    if ($sessionId -ne $currentSessionId) {
        throw "M3 evidence cannot be sealed from a different Windows session. baselineSession=$sessionId currentSession=$currentSessionId"
    }
    $sessionId
}

function Assert-HumanVisualAttestation([string]$DiagnosticsDir, [hashtable]$BinaryCheckpoint, [int]$BaselineSessionId) {
    $path = Join-Path $DiagnosticsDir 'widget-acceptance-human-visual.json'
    $sealPath = "$path.sha256"
    if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or -not (Test-Path -LiteralPath $sealPath -PathType Leaf)) {
        throw 'M3 evidence cannot be sealed before explicit human visual acceptance is recorded with scripts/confirm-widget-visual-acceptance.ps1.'
    }
    $seal = Read-KeyValueFile -Path $sealPath
    $actualHash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    if (-not $seal.ContainsKey('sha256') -or $actualHash -ne ([string]$seal['sha256']).ToLowerInvariant()) {
        throw 'M3 human visual acceptance attestation hash mismatch.'
    }
    $attestation = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
    if ($attestation.schema -ne 'miaodesk.widget-visual-acceptance.v1' -or -not $attestation.reviewer) {
        throw 'M3 human visual acceptance attestation is malformed.'
    }
    if ([int]$attestation.sessionId -ne $BaselineSessionId) {
        throw "M3 human visual acceptance came from a different Windows session. baselineSession=$BaselineSessionId reviewSession=$($attestation.sessionId)"
    }
    if (([string]$attestation.acceptanceBinary.sha256).ToLowerInvariant() -ne ([string]$BinaryCheckpoint['sha256']).ToLowerInvariant() -or [int64]$attestation.acceptanceBinary.length -ne [int64]$BinaryCheckpoint['length']) {
        throw 'M3 human visual acceptance was recorded against a different acceptance binary.'
    }
    foreach ($name in @('wallpaperBelowWidget','iconsAboveWidget','desktopIconsUsable','settingsKeepsWidgetVisible','searchKeepsWidgetVisible','explorerRecoveryVisible','monitorRecoveryVisible')) {
        if (-not [bool]$attestation.confirmations.$name) {
            throw "M3 human visual acceptance is missing required confirmation: $name"
        }
    }
    foreach ($phase in @('baseline','settings','search','explorer','monitor')) {
        $png = Join-Path $DiagnosticsDir "widget-acceptance-$phase.png"
        $actual = (Get-FileHash -LiteralPath $png -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($actual -ne ([string]$attestation.screenshotSha256.$phase).ToLowerInvariant()) {
            throw "M3 human visual acceptance references a different screenshot for phase '$phase'."
        }
    }
    $attestation
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

$baselineSessionId = Read-BaselineSessionId -DiagnosticsDir $diagnostics
$binaryCheckpointPath = Join-Path $diagnostics 'widget-acceptance-binary.sha256'
$binaryCheckpoint = Read-KeyValueFile -Path $binaryCheckpointPath
if (-not $binaryCheckpoint.ContainsKey('sha256') -or -not $binaryCheckpoint.ContainsKey('length')) {
    throw 'M3 acceptance binary checkpoint is incomplete; start a new baseline with the intended MiaoDeskWidgetAcceptance.exe.'
}
$humanAttestation = Assert-HumanVisualAttestation -DiagnosticsDir $diagnostics -BinaryCheckpoint $binaryCheckpoint -BaselineSessionId $baselineSessionId

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

$configEntry = @($files | Where-Object { $_.path -eq 'widget-acceptance-baseline.config' }) | Select-Object -First 1
if ($null -eq $configEntry) { throw 'M3 acceptance placement configuration checkpoint is missing from the evidence set.' }

$manifest = [ordered]@{
    schema = 'miaodesk.widget-acceptance-evidence.v1'
    sealedAtUtc = [DateTime]::UtcNow.ToString('o')
    sequence = $sequence
    baselineSessionId = $baselineSessionId
    placementConfig = [ordered]@{
        fileName = 'widget-acceptance-baseline.config'
        sha256 = [string]$configEntry.sha256
        length = [int64]$configEntry.length
    }
    acceptanceBinary = [ordered]@{
        fileName = 'MiaoDeskWidgetAcceptance.exe'
        sha256 = ([string]$binaryCheckpoint['sha256']).ToLowerInvariant()
        length = [int64]$binaryCheckpoint['length']
    }
    observedProductWindows = [ordered]@{
        settings = 'widget-acceptance-settings.window.json'
        search = 'widget-acceptance-search.window.json'
    }
    humanVisualAcceptance = [ordered]@{
        reviewer = [string]$humanAttestation.reviewer
        reviewedAtUtc = [string]$humanAttestation.reviewedAtUtc
        sessionId = [int]$humanAttestation.sessionId
    }
    baselineWidgetIds = $baselineIds
    machine = [ordered]@{
        computerName = $env:COMPUTERNAME
        userDomain = $env:USERDOMAIN
        sessionId = $baselineSessionId
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
    "schema=miaodesk.widget-acceptance-evidence.v1",
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
Write-Host "Windows session: $baselineSessionId"
Write-Host "Human reviewer: $($manifest.humanVisualAcceptance.reviewer)"
Write-Host "Placement config SHA-256: $($manifest.placementConfig.sha256)"
Write-Host "Acceptance binary SHA-256: $($manifest.acceptanceBinary.sha256)"
Write-Host "Manifest SHA-256: $manifestHash"