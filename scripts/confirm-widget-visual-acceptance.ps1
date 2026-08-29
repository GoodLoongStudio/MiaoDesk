param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$Reviewer,

    [Parameter(Mandatory = $true)]
    [switch]$WallpaperBelowWidget,

    [Parameter(Mandatory = $true)]
    [switch]$IconsAboveWidget,

    [Parameter(Mandatory = $true)]
    [switch]$DesktopIconsUsable,

    [Parameter(Mandatory = $true)]
    [switch]$SettingsKeepsWidgetVisible,

    [Parameter(Mandatory = $true)]
    [switch]$SearchKeepsWidgetVisible,

    [Parameter(Mandatory = $true)]
    [switch]$ExplorerRecoveryVisible,

    [Parameter(Mandatory = $true)]
    [switch]$MonitorRecoveryVisible
)

$ErrorActionPreference = 'Stop'

function Get-DiagnosticsDirectory {
    $base = if ($env:LOCALAPPDATA) { $env:LOCALAPPDATA } else { [IO.Path]::GetTempPath() }
    Join-Path (Join-Path $base 'MiaoDesk') 'Diagnostics'
}

function Read-KeyValueFile([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Required M3 acceptance evidence is missing: $Path"
    }
    $map = @{}
    foreach ($line in Get-Content -LiteralPath $Path -ErrorAction Stop) {
        $trimmed = $line.Trim()
        if (-not $trimmed -or $trimmed.StartsWith('#')) { continue }
        $parts = $trimmed.Split('=', 2)
        if ($parts.Count -eq 2) { $map[$parts[0]] = $parts[1] }
    }
    $map
}

function Read-BaselineSessionId([string]$DiagnosticsDir) {
    $path = Join-Path $DiagnosticsDir 'widget-acceptance-baseline.session'
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw 'M3 baseline Windows session checkpoint is missing. Start a new baseline in the interactive session used for the full sequence.'
    }
    $value = (Get-Content -LiteralPath $path -Raw -ErrorAction Stop).Trim()
    $sessionId = 0
    if (-not [int]::TryParse($value, [ref]$sessionId) -or $sessionId -lt 0) {
        throw "M3 baseline Windows session checkpoint is malformed: '$value'"
    }
    $sessionId
}

foreach ($flag in @(
    $WallpaperBelowWidget,
    $IconsAboveWidget,
    $DesktopIconsUsable,
    $SettingsKeepsWidgetVisible,
    $SearchKeepsWidgetVisible,
    $ExplorerRecoveryVisible,
    $MonitorRecoveryVisible)) {
    if (-not $flag.IsPresent) {
        throw 'All visual acceptance confirmations are mandatory. Inspect the real interactive Windows desktop and all five phase screenshots before confirming.'
    }
}

$diagnostics = Get-DiagnosticsDirectory
$sequencePath = Join-Path $diagnostics 'widget-acceptance-sequence.phase'
if (-not (Test-Path -LiteralPath $sequencePath -PathType Leaf)) {
    throw 'M3 acceptance sequence is missing. Complete baseline -> settings -> search -> explorer -> monitor first.'
}
$sequence = (Get-Content -LiteralPath $sequencePath -Raw).Trim()
if ($sequence -ne 'monitor') {
    throw "Visual acceptance cannot be confirmed before the complete monitor phase. Current sequence='$sequence'."
}

$baselineSessionId = Read-BaselineSessionId -DiagnosticsDir $diagnostics
$currentSessionId = [System.Diagnostics.Process]::GetCurrentProcess().SessionId
if ($currentSessionId -ne $baselineSessionId) {
    throw "Human visual acceptance must be recorded in the same Windows session as the M3 baseline. baselineSession=$baselineSessionId currentSession=$currentSessionId"
}

$binary = Read-KeyValueFile -Path (Join-Path $diagnostics 'widget-acceptance-binary.sha256')
if (-not $binary.ContainsKey('sha256') -or -not $binary.ContainsKey('length')) {
    throw 'Acceptance binary checkpoint is malformed.'
}

$phaseHashes = [ordered]@{}
foreach ($phase in @('baseline','settings','search','explorer','monitor')) {
    $pngPath = Join-Path $diagnostics "widget-acceptance-$phase.png"
    $sidecarPath = "$pngPath.sha256"
    if (-not (Test-Path -LiteralPath $pngPath -PathType Leaf)) {
        throw "Visual acceptance screenshot is missing for phase '$phase': $pngPath"
    }
    $sidecar = Read-KeyValueFile -Path $sidecarPath
    if ($sidecar['phase'] -ne $phase -or -not $sidecar.ContainsKey('sha256')) {
        throw "Visual acceptance screenshot sidecar is malformed for phase '$phase'."
    }
    $actual = (Get-FileHash -LiteralPath $pngPath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne ([string]$sidecar['sha256']).ToLowerInvariant()) {
        throw "Visual acceptance screenshot hash mismatch for phase '$phase'."
    }
    $phaseHashes[$phase] = $actual
}

$attestation = [ordered]@{
    schema = 'miaodesk.widget-visual-acceptance.v1'
    reviewedAtUtc = [DateTime]::UtcNow.ToString('o')
    reviewer = $Reviewer.Trim()
    sessionId = $baselineSessionId
    acceptanceBinary = [ordered]@{
        sha256 = ([string]$binary['sha256']).ToLowerInvariant()
        length = [int64]$binary['length']
    }
    confirmations = [ordered]@{
        wallpaperBelowWidget = $true
        iconsAboveWidget = $true
        desktopIconsUsable = $true
        settingsKeepsWidgetVisible = $true
        searchKeepsWidgetVisible = $true
        explorerRecoveryVisible = $true
        monitorRecoveryVisible = $true
    }
    screenshotSha256 = $phaseHashes
}

New-Item -ItemType Directory -Path $diagnostics -Force | Out-Null
$path = Join-Path $diagnostics 'widget-acceptance-human-visual.json'
$attestation | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $path -Encoding utf8
$hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
Set-Content -LiteralPath "$path.sha256" -Value @(
    "sha256=$hash",
    'schema=miaodesk.widget-visual-acceptance.v1',
    "reviewedAtUtc=$($attestation.reviewedAtUtc)",
    "sessionId=$baselineSessionId"
) -Encoding utf8

Write-Host "Recorded explicit M3 human visual acceptance: $path"
Write-Host "Windows session: $baselineSessionId"
Write-Host "Reviewer: $($attestation.reviewer)"
Write-Host "Attestation SHA-256: $hash"