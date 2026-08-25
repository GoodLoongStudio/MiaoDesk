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
    'turingdesk.widget-acceptance-evidence.v1',
    'widget-acceptance-evidence.manifest.json',
    'widget-acceptance-evidence.manifest.sha256',
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
    'capturedAtUtc')) {
    if (-not $verifierText.Contains($marker)) {
        throw "M3 evidence verifier missing integrity marker: $marker"
    }
}
foreach ($forbidden in @('FindWindowW(', 'FindWindowExW(', 'EnumWindows(', 'SetParent(', 'SetWindowPos(', 'Progman', 'WorkerW', 'SHELLDLL_DefView')) {
    if ($verifierText.Contains($forbidden)) {
        throw "M3 evidence verifier must remain diagnostics-only: $forbidden"
    }
}

$sealerText = Get-Content -LiteralPath $sealer -Raw
foreach ($marker in @('verify-widget-acceptance-evidence.ps1', 'Sealed and verified M3 Widget acceptance evidence')) {
    if (-not $sealerText.Contains($marker)) {
        throw "M3 evidence sealer must self-verify after writing the seal: $marker"
    }
}

Write-Host 'M3 evidence verifier contract OK: sealed evidence can be independently rehashed and phase/recovery artifacts are checked without regaining shell ownership.'
