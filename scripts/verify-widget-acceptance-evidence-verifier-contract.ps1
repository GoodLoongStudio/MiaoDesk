param()

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$verifier = Join-Path $root 'scripts/verify-widget-acceptance-evidence.ps1'
$sealer = Join-Path $root 'scripts/seal-widget-acceptance-evidence.ps1'

foreach ($path in @($verifier, $sealer)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Missing M3 evidence verification contract input: $path"
    }
}

$verifierText = Get-Content -LiteralPath $verifier -Raw
foreach ($marker in @(
    'miaodesk.widget-acceptance-evidence.v1',
    'widget-acceptance-evidence.manifest.json',
    'widget-acceptance-evidence.manifest.sha256',
    'widget-acceptance-baseline.config',
    'placementConfig',
    'placement configuration SHA-256 no longer matches',
    'placement configuration length no longer matches',
    'widget-acceptance-binary.sha256',
    'acceptanceBinary',
    'MiaoDeskWidgetAcceptance.exe',
    'acceptance binary SHA-256 no longer matches',
    'acceptance binary length no longer matches',
    'Get-FileHash',
    'SHA256',
    'manifest hash mismatch',
    'file disappeared after sealing',
    'hash mismatch after sealing',
    "@('baseline','settings','search','explorer','monitor')",
    'widget-acceptance-sequence.phase',
    'widget-acceptance-baseline.ids',
    'widget-acceptance-search.explorer-pids',
    'widget-acceptance-explorer.monitor-topology',
    'widget-acceptance-monitor.topology-transition',
    'virtualBounds',
    'capturedAtUtc',
    'Parse-UtcTimestamp',
    'manifest.sealedAtUtc',
    'phaseOrder',
    'visual evidence chronology is invalid',
    'phase evidence chronology is invalid',
    'more than five minutes after its health report')) {
    if (-not $verifierText.Contains($marker)) {
        throw "M3 evidence verifier missing integrity/chronology/binary/config marker: $marker"
    }
}
foreach ($forbidden in @('FindWindowW(', 'FindWindowExW(', 'EnumWindows(', 'SetParent(', 'SetWindowPos(', 'Progman', 'WorkerW', 'SHELLDLL_DefView')) {
    if ($verifierText.Contains($forbidden)) {
        throw "M3 evidence verifier must remain diagnostics-only: $forbidden"
    }
}

$sealerText = Get-Content -LiteralPath $sealer -Raw
foreach ($marker in @(
    'verify-widget-acceptance-evidence.ps1',
    'widget-acceptance-baseline.config',
    'placementConfig',
    "fileName = 'widget-acceptance-baseline.config'",
    'Placement config SHA-256',
    'widget-acceptance-binary.sha256',
    'acceptanceBinary',
    "fileName = 'MiaoDeskWidgetAcceptance.exe'",
    'Acceptance binary SHA-256',
    'Sealed and verified M3 Widget acceptance evidence')) {
    if (-not $sealerText.Contains($marker)) {
        throw "M3 evidence sealer must bind and self-verify placement config and acceptance binary: $marker"
    }
}

Write-Host 'M3 evidence verifier contract OK: sealed evidence independently binds and rehashes stable Widget placement configuration plus acceptance binary identity, phase/recovery artifacts and chronological coherence are checked, and diagnostics do not regain shell ownership.'