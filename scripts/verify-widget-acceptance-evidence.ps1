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

function Assert-ObservedProductWindow([string]$DiagnosticsDir, [string]$Phase, [string]$ExpectedClass, [string]$ExpectedProcess, [int]$ExpectedSessionId, [DateTimeOffset]$SealedAtUtc) {
    $name = "widget-acceptance-$Phase.window.json"
    $path = Join-Path $DiagnosticsDir $name
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "M3 observed product-window evidence is missing: $name" }
    $evidence = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
    if ($evidence.schema -ne 'turingdesk.widget-window-evidence.v1') { throw "Unexpected M3 product-window evidence schema for ${Phase}: '$($evidence.schema)'" }
    if ([string]$evidence.phase -ne $Phase) { throw "M3 product-window evidence phase mismatch for $Phase." }
    if ([string]$evidence.className -ne $ExpectedClass) { throw "M3 $Phase window evidence class mismatch: '$($evidence.className)'" }
    if ([string]$evidence.processName -ne $ExpectedProcess) { throw "M3 $Phase window evidence process mismatch: '$($evidence.processName)'" }
    if ([int]$evidence.sessionId -ne $ExpectedSessionId) { throw "M3 $Phase window evidence came from a different Windows session." }
    $capturedAtUtc = Parse-UtcTimestamp -Value ([string]$evidence.capturedAtUtc) -Description "$Phase observed product window"
    if ($capturedAtUtc -gt $SealedAtUtc) { throw "M3 $Phase window evidence was captured after the manifest seal." }
    return $capturedAtUtc
}

$diagnostics = Get-DiagnosticsDirectory
$manifestPath = Join-Path $diagnostics 'widget-acceptance-evidence.manifest.json'
$sealPath = Join-Path $diagnostics 'widget-acceptance-evidence.manifest.sha256'
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) { throw "M3 sealed evidence manifest is missing: $manifestPath" }
if (-not (Test-Path -LiteralPath $sealPath -PathType Leaf)) { throw "M3 sealed evidence manifest hash is missing: $sealPath" }

$seal = Read-KeyValueFile -Path $sealPath
if ($seal['schema'] -ne 'turingdesk.widget-acceptance-evidence.v1') { throw "Unexpected M3 acceptance evidence schema in seal file: '$($seal['schema'])'" }
if (-not $seal.ContainsKey('sha256')) { throw 'M3 acceptance evidence seal is missing sha256.' }
$actualManifestHash = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash.ToLowerInvariant()
if ($actualManifestHash -ne ([string]$seal['sha256']).ToLowerInvariant()) { throw 'M3 acceptance evidence manifest hash mismatch; the sealed manifest changed after sealing.' }

$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($manifest.schema -ne 'turingdesk.widget-acceptance-evidence.v1') { throw "Unexpected M3 acceptance evidence manifest schema: '$($manifest.schema)'" }
if ($manifest.sequence -ne 'monitor') { throw "M3 acceptance evidence is not a completed sequence: '$($manifest.sequence)'" }
if ($null -eq $manifest.files -or @($manifest.files).Count -eq 0) { throw 'M3 acceptance evidence manifest contains no files.' }
if ($null -eq $manifest.placementConfig -or -not $manifest.placementConfig.sha256 -or -not $manifest.placementConfig.length -or $manifest.placementConfig.fileName -ne 'widget-acceptance-baseline.config') { throw 'M3 acceptance evidence manifest is missing the placement configuration identity.' }
if ($null -eq $manifest.acceptanceBinary -or -not $manifest.acceptanceBinary.sha256 -or -not $manifest.acceptanceBinary.length) { throw 'M3 acceptance evidence manifest is missing the acceptance binary identity.' }
if ($null -eq $manifest.observedProductWindows -or $manifest.observedProductWindows.settings -ne 'widget-acceptance-settings.window.json' -or $manifest.observedProductWindows.search -ne 'widget-acceptance-search.window.json') { throw 'M3 acceptance evidence manifest is missing observed Settings/Search product-window evidence mapping.' }
if ($null -eq $manifest.humanVisualAcceptance -or -not $manifest.humanVisualAcceptance.reviewer -or -not $manifest.humanVisualAcceptance.reviewedAtUtc) { throw 'M3 acceptance evidence manifest is missing explicit human visual acceptance.' }
if ($null -eq $manifest.baselineSessionId) { throw 'M3 acceptance evidence manifest is missing baseline Windows session identity.' }
$sealedAtUtc = Parse-UtcTimestamp -Value ([string]$manifest.sealedAtUtc) -Description 'manifest.sealedAtUtc'
$reviewedAtUtc = Parse-UtcTimestamp -Value ([string]$manifest.humanVisualAcceptance.reviewedAtUtc) -Description 'manifest.humanVisualAcceptance.reviewedAtUtc'
if ($reviewedAtUtc -gt $sealedAtUtc) { throw 'M3 visual acceptance review timestamp is after the manifest seal.' }

$seen = @{}
foreach ($entry in @($manifest.files)) {
    $name = [string]$entry.path
    if (-not $name) { throw 'M3 acceptance evidence manifest contains a file entry without a path.' }
    if ($seen.ContainsKey($name)) { throw "Duplicate M3 acceptance evidence entry: $name" }
    $seen[$name] = $true
    $path = Join-Path $diagnostics $name
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "M3 acceptance evidence file disappeared after sealing: $name" }
    $item = Get-Item -LiteralPath $path
    if ([int64]$item.Length -ne [int64]$entry.length) { throw "M3 acceptance evidence length mismatch after sealing: $name" }
    $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($hash -ne ([string]$entry.sha256).ToLowerInvariant()) { throw "M3 acceptance evidence hash mismatch after sealing: $name" }
    $entryWriteUtc = Parse-UtcTimestamp -Value ([string]$entry.lastWriteUtc) -Description "$name lastWriteUtc"
    if ($entryWriteUtc -gt $sealedAtUtc.AddSeconds(1)) { throw "M3 acceptance evidence file is timestamped after the manifest seal: $name" }
}

$baselineSessionName = 'widget-acceptance-baseline.session'
if (-not $seen.ContainsKey($baselineSessionName)) { throw "M3 sealed evidence manifest is missing required Windows session checkpoint: $baselineSessionName" }
$baselineSessionText = (Get-Content -LiteralPath (Join-Path $diagnostics $baselineSessionName) -Raw).Trim()
$baselineSessionId = 0
if (-not [int]::TryParse($baselineSessionText, [ref]$baselineSessionId) -or $baselineSessionId -lt 0) { throw "M3 baseline Windows session checkpoint is malformed: '$baselineSessionText'" }
if ($baselineSessionId -ne [int]$manifest.baselineSessionId -or $baselineSessionId -ne [int]$manifest.machine.sessionId) { throw 'M3 baseline Windows session checkpoint no longer matches the sealed manifest.' }

$placementConfigName = 'widget-acceptance-baseline.config'
if (-not $seen.ContainsKey($placementConfigName)) { throw "M3 sealed evidence manifest is missing required placement configuration checkpoint: $placementConfigName" }
$placementConfigPath = Join-Path $diagnostics $placementConfigName
$placementConfigItem = Get-Item -LiteralPath $placementConfigPath
$placementConfigHash = (Get-FileHash -LiteralPath $placementConfigPath -Algorithm SHA256).Hash.ToLowerInvariant()
if ($placementConfigHash -ne ([string]$manifest.placementConfig.sha256).ToLowerInvariant()) { throw 'M3 placement configuration SHA-256 no longer matches the sealed manifest.' }
if ([int64]$placementConfigItem.Length -ne [int64]$manifest.placementConfig.length) { throw 'M3 placement configuration length no longer matches the sealed manifest.' }

$sessionVerifier = Join-Path $PSScriptRoot 'verify-widget-acceptance-session-evidence.ps1'
if (-not (Test-Path -LiteralPath $sessionVerifier -PathType Leaf)) { throw "M3 session/phase health verifier is missing: $sessionVerifier" }
& $sessionVerifier -DiagnosticsDir $diagnostics
if ($LASTEXITCODE -ne 0) { throw "M3 session/phase health verifier failed with exit code $LASTEXITCODE" }

$binaryCheckpointName = 'widget-acceptance-binary.sha256'
if (-not $seen.ContainsKey($binaryCheckpointName)) { throw "M3 sealed evidence manifest is missing required acceptance binary checkpoint: $binaryCheckpointName" }
$binaryCheckpoint = Read-KeyValueFile -Path (Join-Path $diagnostics $binaryCheckpointName)
if (-not $binaryCheckpoint.ContainsKey('sha256') -or -not $binaryCheckpoint.ContainsKey('length')) { throw 'M3 acceptance binary checkpoint is malformed.' }
if (([string]$binaryCheckpoint['sha256']).ToLowerInvariant() -ne ([string]$manifest.acceptanceBinary.sha256).ToLowerInvariant()) { throw 'M3 acceptance binary SHA-256 no longer matches the sealed manifest.' }
if ([int64]$binaryCheckpoint['length'] -ne [int64]$manifest.acceptanceBinary.length) { throw 'M3 acceptance binary length no longer matches the sealed manifest.' }
if ([string]$manifest.acceptanceBinary.fileName -ne 'TuringDeskWidgetAcceptance.exe') { throw "Unexpected M3 acceptance binary name: '$($manifest.acceptanceBinary.fileName)'" }

$phaseOrder = @('baseline','settings','search','explorer','monitor')
$previousCaptureUtc = $null
$phaseCaptureUtc = @{}
foreach ($phase in $phaseOrder) {
    foreach ($suffix in @('.txt','.png','.png.sha256')) {
        $name = "widget-acceptance-$phase$suffix"
        if (-not $seen.ContainsKey($name)) { throw "M3 sealed evidence manifest is missing required phase artifact: $name" }
    }
    $sidecar = Read-KeyValueFile -Path (Join-Path $diagnostics "widget-acceptance-$phase.png.sha256")
    if ($sidecar['phase'] -ne $phase) { throw "M3 visual evidence sidecar phase mismatch for $phase." }
    $pngPath = Join-Path $diagnostics "widget-acceptance-$phase.png"
    $pngHash = (Get-FileHash -LiteralPath $pngPath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($pngHash -ne ([string]$sidecar['sha256']).ToLowerInvariant()) { throw "M3 visual evidence PNG hash mismatch for $phase." }
    if (-not $sidecar.ContainsKey('virtualBounds') -or -not $sidecar.ContainsKey('capturedAtUtc')) { throw "M3 visual evidence sidecar metadata is incomplete for $phase." }
    $capturedAtUtc = Parse-UtcTimestamp -Value ([string]$sidecar['capturedAtUtc']) -Description "$phase capturedAtUtc"
    $phaseCaptureUtc[$phase] = $capturedAtUtc
    if ($null -ne $previousCaptureUtc -and $capturedAtUtc -le $previousCaptureUtc) { throw "M3 visual evidence chronology is invalid: $phase was not captured after the previous successful phase." }
    if ($capturedAtUtc -gt $sealedAtUtc) { throw "M3 visual evidence chronology is invalid: $phase capture is after the manifest seal." }
    $reportPath = Join-Path $diagnostics "widget-acceptance-$phase.txt"
    $reportWriteUtc = (Get-Item -LiteralPath $reportPath).LastWriteTimeUtc
    $captureUtcDateTime = $capturedAtUtc.UtcDateTime
    if ($reportWriteUtc -gt $captureUtcDateTime.AddSeconds(5)) { throw "M3 phase evidence chronology is invalid: $phase report was written after its visual capture." }
    if ($captureUtcDateTime -gt $reportWriteUtc.AddMinutes(5)) { throw "M3 phase evidence chronology is suspicious: $phase visual capture is more than five minutes after its health report." }
    $previousCaptureUtc = $capturedAtUtc
}

foreach ($required in @('widget-acceptance-baseline.ids','widget-acceptance-baseline.session','widget-acceptance-baseline.config','widget-acceptance-sequence.phase','widget-acceptance-settings.window.json','widget-acceptance-search.window.json','widget-acceptance-search.explorer-pids','widget-acceptance-explorer.monitor-topology','widget-acceptance-monitor.topology-transition','widget-acceptance-human-visual.json','widget-acceptance-human-visual.json.sha256')) {
    if (-not $seen.ContainsKey($required)) { throw "M3 sealed evidence manifest is missing required recovery/visual/window/session/config evidence: $required" }
}

$expectedSessionId = $baselineSessionId
$settingsWindowUtc = Assert-ObservedProductWindow -DiagnosticsDir $diagnostics -Phase 'settings' -ExpectedClass 'TuringDesk.Native.DesktopLibrary' -ExpectedProcess 'TuringDeskWallpaper' -ExpectedSessionId $expectedSessionId -SealedAtUtc $sealedAtUtc
$searchWindowUtc = Assert-ObservedProductWindow -DiagnosticsDir $diagnostics -Phase 'search' -ExpectedClass 'TuringDesk.Native.SearchWindow' -ExpectedProcess 'TuringDesk' -ExpectedSessionId $expectedSessionId -SealedAtUtc $sealedAtUtc
if ($settingsWindowUtc -le $phaseCaptureUtc['baseline']) { throw 'M3 Settings product-window evidence was not observed after the baseline screenshot.' }
if ($settingsWindowUtc -gt $phaseCaptureUtc['settings']) { throw 'M3 Settings product-window evidence was captured after the Settings phase screenshot.' }
if ($searchWindowUtc -le $phaseCaptureUtc['settings']) { throw 'M3 Search product-window evidence was not observed after the Settings phase screenshot.' }
if ($searchWindowUtc -gt $phaseCaptureUtc['search']) { throw 'M3 Search product-window evidence was captured after the Search phase screenshot.' }
if ($searchWindowUtc -le $settingsWindowUtc) { throw 'M3 observed product-window evidence chronology is invalid: Search was not observed after Settings.' }

$attestationPath = Join-Path $diagnostics 'widget-acceptance-human-visual.json'
$attestationSeal = Read-KeyValueFile -Path "$attestationPath.sha256"
$attestationHash = (Get-FileHash -LiteralPath $attestationPath -Algorithm SHA256).Hash.ToLowerInvariant()
if ($attestationSeal['schema'] -ne 'turingdesk.widget-visual-acceptance.v1' -or $attestationHash -ne ([string]$attestationSeal['sha256']).ToLowerInvariant()) { throw 'M3 human visual acceptance seal is invalid.' }
$attestation = Get-Content -LiteralPath $attestationPath -Raw | ConvertFrom-Json
if ($attestation.schema -ne 'turingdesk.widget-visual-acceptance.v1' -or [string]$attestation.reviewer -ne [string]$manifest.humanVisualAcceptance.reviewer -or [string]$attestation.reviewedAtUtc -ne [string]$manifest.humanVisualAcceptance.reviewedAtUtc) { throw 'M3 human visual acceptance metadata no longer matches the sealed manifest.' }
if (([string]$attestation.acceptanceBinary.sha256).ToLowerInvariant() -ne ([string]$manifest.acceptanceBinary.sha256).ToLowerInvariant() -or [int64]$attestation.acceptanceBinary.length -ne [int64]$manifest.acceptanceBinary.length) { throw 'M3 human visual acceptance binary identity does not match the sealed manifest.' }
foreach ($name in @('wallpaperBelowWidget','iconsAboveWidget','desktopIconsUsable','settingsKeepsWidgetVisible','searchKeepsWidgetVisible','explorerRecoveryVisible','monitorRecoveryVisible')) {
    if (-not [bool]$attestation.confirmations.$name) { throw "M3 human visual acceptance is missing confirmation: $name" }
}
foreach ($phase in $phaseOrder) {
    $actual = (Get-FileHash -LiteralPath (Join-Path $diagnostics "widget-acceptance-$phase.png") -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne ([string]$attestation.screenshotSha256.$phase).ToLowerInvariant()) { throw "M3 human visual acceptance screenshot identity mismatch for phase '$phase'." }
}
if ($reviewedAtUtc -lt $previousCaptureUtc) { throw 'M3 human visual acceptance was recorded before the final monitor screenshot existed.' }

$sequence = (Get-Content -LiteralPath (Join-Path $diagnostics 'widget-acceptance-sequence.phase') -Raw).Trim()
if ($sequence -ne 'monitor') { throw "M3 acceptance sequence cursor changed after sealing: '$sequence'" }
$baselineIds = @(Get-Content -LiteralPath (Join-Path $diagnostics 'widget-acceptance-baseline.ids') | ForEach-Object { $_.Trim() } | Where-Object { $_ } | Sort-Object -Unique)
$manifestIds = @($manifest.baselineWidgetIds | ForEach-Object { [string]$_ } | Sort-Object -Unique)
if (($baselineIds -join "`n") -ne ($manifestIds -join "`n")) { throw 'M3 baseline Widget identity set no longer matches the sealed manifest.' }

Write-Host "Verified sealed M3 Widget acceptance evidence integrity, same-session healthy phase reports, stable placement configuration, observed Settings/Search product windows, binary continuity, phase chronology and explicit human visual acceptance: $manifestPath"
Write-Host "Windows session: $baselineSessionId"
Write-Host "Placement config SHA-256: $placementConfigHash"
Write-Host "Human reviewer: $($manifest.humanVisualAcceptance.reviewer)"
Write-Host "Acceptance binary SHA-256: $($manifest.acceptanceBinary.sha256)"
Write-Host "Manifest SHA-256: $actualManifestHash"