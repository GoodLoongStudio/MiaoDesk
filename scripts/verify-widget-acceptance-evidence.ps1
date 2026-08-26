param()

$ErrorActionPreference = 'Stop'

function Get-DiagnosticsDirectory {
    $base = if ($env:LOCALAPPDATA) { $env:LOCALAPPDATA } else { [IO.Path]::GetTempPath() }
    Join-Path (Join-Path $base 'TuringDesk') 'Diagnostics'
}

function Read-KeyValueFile([string]$Path) {
    $map = @{}
    foreach ($line in Get-Content -LiteralPath $Path -ErrorAction Stop) {
        $trimmed = $line.Trim()
        if (-not $trimmed -or $trimmed.StartsWith('#')) { continue }
        $parts = $trimmed.Split('=', 2)
        if ($parts.Count -eq 2) { $map[$parts[0]] = $parts[1] }
    }
    $map
}

function Parse-UtcTimestamp([string]$Value, [string]$Description) {
    if (-not $Value) { throw "M3 acceptance evidence is missing timestamp: $Description" }
    $parsed = [DateTimeOffset]::MinValue
    if (-not [DateTimeOffset]::TryParse($Value, [Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::RoundtripKind, [ref]$parsed)) {
        throw "M3 acceptance evidence has invalid timestamp for ${Description}: '$Value'"
    }
    $parsed.ToUniversalTime()
}

$diagnostics = Get-DiagnosticsDirectory
$manifestPath = Join-Path $diagnostics 'widget-acceptance-evidence.manifest.json'
$sealPath = Join-Path $diagnostics 'widget-acceptance-evidence.manifest.sha256'
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw "M3 sealed evidence manifest is missing: $manifestPath"
}
if (-not (Test-Path -LiteralPath $sealPath -PathType Leaf)) {
    throw "M3 sealed evidence manifest hash is missing: $sealPath"
}

$seal = Read-KeyValueFile -Path $sealPath
if ($seal['schema'] -ne 'turingdesk.widget-acceptance-evidence.v1') {
    throw "Unexpected M3 acceptance evidence schema in seal file: '$($seal['schema'])'"
}
if (-not $seal.ContainsKey('sha256')) {
    throw 'M3 acceptance evidence seal is missing sha256.'
}
$actualManifestHash = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash.ToLowerInvariant()
if ($actualManifestHash -ne ([string]$seal['sha256']).ToLowerInvariant()) {
    throw 'M3 acceptance evidence manifest hash mismatch; the sealed manifest changed after sealing.'
}

$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($manifest.schema -ne 'turingdesk.widget-acceptance-evidence.v1') {
    throw "Unexpected M3 acceptance evidence manifest schema: '$($manifest.schema)'"
}
if ($manifest.sequence -ne 'monitor') {
    throw "M3 acceptance evidence is not a completed sequence: '$($manifest.sequence)'"
}
if ($null -eq $manifest.files -or @($manifest.files).Count -eq 0) {
    throw 'M3 acceptance evidence manifest contains no files.'
}
if ($null -eq $manifest.acceptanceBinary -or -not $manifest.acceptanceBinary.sha256 -or -not $manifest.acceptanceBinary.length) {
    throw 'M3 acceptance evidence manifest is missing the acceptance binary identity.'
}
$sealedAtUtc = Parse-UtcTimestamp -Value ([string]$manifest.sealedAtUtc) -Description 'manifest.sealedAtUtc'

$seen = @{}
foreach ($entry in @($manifest.files)) {
    $name = [string]$entry.path
    if (-not $name) { throw 'M3 acceptance evidence manifest contains a file entry without a path.' }
    if ($seen.ContainsKey($name)) { throw "Duplicate M3 acceptance evidence entry: $name" }
    $seen[$name] = $true
    $path = Join-Path $diagnostics $name
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "M3 acceptance evidence file disappeared after sealing: $name"
    }
    $item = Get-Item -LiteralPath $path
    if ([int64]$item.Length -ne [int64]$entry.length) {
        throw "M3 acceptance evidence length mismatch after sealing: $name"
    }
    $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($hash -ne ([string]$entry.sha256).ToLowerInvariant()) {
        throw "M3 acceptance evidence hash mismatch after sealing: $name"
    }
    $entryWriteUtc = Parse-UtcTimestamp -Value ([string]$entry.lastWriteUtc) -Description "$name lastWriteUtc"
    if ($entryWriteUtc -gt $sealedAtUtc.AddSeconds(1)) {
        throw "M3 acceptance evidence file is timestamped after the manifest seal: $name"
    }
}

$binaryCheckpointName = 'widget-acceptance-binary.sha256'
if (-not $seen.ContainsKey($binaryCheckpointName)) {
    throw "M3 sealed evidence manifest is missing required acceptance binary checkpoint: $binaryCheckpointName"
}
$binaryCheckpoint = Read-KeyValueFile -Path (Join-Path $diagnostics $binaryCheckpointName)
if (-not $binaryCheckpoint.ContainsKey('sha256') -or -not $binaryCheckpoint.ContainsKey('length')) {
    throw 'M3 acceptance binary checkpoint is malformed.'
}
if (([string]$binaryCheckpoint['sha256']).ToLowerInvariant() -ne ([string]$manifest.acceptanceBinary.sha256).ToLowerInvariant()) {
    throw 'M3 acceptance binary SHA-256 no longer matches the sealed manifest.'
}
if ([int64]$binaryCheckpoint['length'] -ne [int64]$manifest.acceptanceBinary.length) {
    throw 'M3 acceptance binary length no longer matches the sealed manifest.'
}
if ([string]$manifest.acceptanceBinary.fileName -ne 'TuringDeskWidgetAcceptance.exe') {
    throw "Unexpected M3 acceptance binary name: '$($manifest.acceptanceBinary.fileName)'"
}

$phaseOrder = @('baseline','settings','search','explorer','monitor')
$previousCaptureUtc = $null
foreach ($phase in $phaseOrder) {
    foreach ($suffix in @('.txt','.png','.png.sha256')) {
        $name = "widget-acceptance-$phase$suffix"
        if (-not $seen.ContainsKey($name)) {
            throw "M3 sealed evidence manifest is missing required phase artifact: $name"
        }
    }
    $sidecar = Read-KeyValueFile -Path (Join-Path $diagnostics "widget-acceptance-$phase.png.sha256")
    if ($sidecar['phase'] -ne $phase) {
        throw "M3 visual evidence sidecar phase mismatch for $phase."
    }
    $pngPath = Join-Path $diagnostics "widget-acceptance-$phase.png"
    $pngHash = (Get-FileHash -LiteralPath $pngPath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($pngHash -ne ([string]$sidecar['sha256']).ToLowerInvariant()) {
        throw "M3 visual evidence PNG hash mismatch for $phase."
    }
    if (-not $sidecar.ContainsKey('virtualBounds') -or -not $sidecar.ContainsKey('capturedAtUtc')) {
        throw "M3 visual evidence sidecar metadata is incomplete for $phase."
    }

    $capturedAtUtc = Parse-UtcTimestamp -Value ([string]$sidecar['capturedAtUtc']) -Description "$phase capturedAtUtc"
    if ($null -ne $previousCaptureUtc -and $capturedAtUtc -le $previousCaptureUtc) {
        throw "M3 visual evidence chronology is invalid: $phase was not captured after the previous successful phase."
    }
    if ($capturedAtUtc -gt $sealedAtUtc) {
        throw "M3 visual evidence chronology is invalid: $phase capture is after the manifest seal."
    }

    $reportPath = Join-Path $diagnostics "widget-acceptance-$phase.txt"
    $reportWriteUtc = (Get-Item -LiteralPath $reportPath).LastWriteTimeUtc
    $captureUtcDateTime = $capturedAtUtc.UtcDateTime
    if ($reportWriteUtc -gt $captureUtcDateTime.AddSeconds(5)) {
        throw "M3 phase evidence chronology is invalid: $phase report was written after its visual capture."
    }
    if ($captureUtcDateTime -gt $reportWriteUtc.AddMinutes(5)) {
        throw "M3 phase evidence chronology is suspicious: $phase visual capture is more than five minutes after its health report."
    }
    $previousCaptureUtc = $capturedAtUtc
}

foreach ($required in @(
    'widget-acceptance-baseline.ids',
    'widget-acceptance-sequence.phase',
    'widget-acceptance-search.explorer-pids',
    'widget-acceptance-explorer.monitor-topology',
    'widget-acceptance-monitor.topology-transition')) {
    if (-not $seen.ContainsKey($required)) {
        throw "M3 sealed evidence manifest is missing required recovery evidence: $required"
    }
}

$sequence = (Get-Content -LiteralPath (Join-Path $diagnostics 'widget-acceptance-sequence.phase') -Raw).Trim()
if ($sequence -ne 'monitor') {
    throw "M3 acceptance sequence cursor changed after sealing: '$sequence'"
}

$baselineIds = @(
    Get-Content -LiteralPath (Join-Path $diagnostics 'widget-acceptance-baseline.ids') |
        ForEach-Object { $_.Trim() } |
        Where-Object { $_ } |
        Sort-Object -Unique
)
$manifestIds = @($manifest.baselineWidgetIds | ForEach-Object { [string]$_ } | Sort-Object -Unique)
if (($baselineIds -join "`n") -ne ($manifestIds -join "`n")) {
    throw 'M3 baseline Widget identity set no longer matches the sealed manifest.'
}

Write-Host "Verified sealed M3 Widget acceptance evidence integrity, binary continuity and phase chronology: $manifestPath"
Write-Host "Acceptance binary SHA-256: $($manifest.acceptanceBinary.sha256)"
Write-Host "Manifest SHA-256: $actualManifestHash"
