$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$RepoRoot = Split-Path $PSScriptRoot -Parent
$BundleRoot = Join-Path $RepoRoot 'runtime\arm64'
$LockPath = Join-Path $BundleRoot 'runtime-lock.json'
$ManifestPath = Join-Path $BundleRoot 'runtime-manifest.json'
$CompleteMarker = Join-Path $BundleRoot '.complete'

function Sha256([string]$Path) {
    return (Get-FileHash -Algorithm SHA256 -Path $Path).Hash.ToLowerInvariant()
}

function Resolve-BundleFile([string]$RelativePath) {
    if ([string]::IsNullOrWhiteSpace($RelativePath)) { throw 'Runtime manifest contains an empty path' }
    $path = Join-Path $BundleRoot ($RelativePath -replace '/', '\')
    if (-not (Test-Path $path -PathType Leaf)) { throw "Vendored runtime file is missing: $path" }
    return $path
}

function Assert-Hash([string]$Path, [string]$Expected) {
    $actual = Sha256 $Path
    if ([string]::IsNullOrWhiteSpace($Expected) -or $actual -ne $Expected.ToLowerInvariant()) {
        throw "Runtime hash mismatch: $Path`nExpected: $Expected`nActual:   $actual"
    }
}

foreach ($required in @($LockPath, $ManifestPath, $CompleteMarker)) {
    if (-not (Test-Path $required -PathType Leaf)) { throw "ARM64 RuntimeBundle is incomplete: $required" }
}

$lock = Get-Content $LockPath -Raw | ConvertFrom-Json
$manifest = Get-Content $ManifestPath -Raw | ConvertFrom-Json
if ($lock.architecture -ne 'arm64' -or $manifest.architecture -ne 'arm64') { throw 'RuntimeBundle architecture is not ARM64' }
if ([int]$manifest.schema -lt 2) { throw 'RuntimeBundle manifest is too old' }

$lockHash = Sha256 $LockPath
if ($manifest.lockSha256.ToLowerInvariant() -ne $lockHash) {
    throw 'RuntimeBundle is stale relative to runtime-lock.json. Vendor ARM64 Runtime Bundle must finish successfully first.'
}

Assert-Hash (Resolve-BundleFile ([string]$manifest.node.archive)) ([string]$manifest.node.sha256)
Assert-Hash (Resolve-BundleFile ([string]$manifest.deepseekHarness.archive)) ([string]$manifest.deepseekHarness.sha256)
Assert-Hash (Resolve-BundleFile ([string]$manifest.goz.archive)) ([string]$manifest.goz.sha256)
Assert-Hash (Resolve-BundleFile ([string]$manifest.pi.archive)) ([string]$manifest.pi.sha256)
Assert-Hash (Resolve-BundleFile ([string]$manifest.webview2Sdk.loader)) ([string]$manifest.webview2Sdk.sha256)

foreach ($retired in @('everything', 'codex', 'codex-relay')) {
    if (Test-Path (Join-Path $BundleRoot $retired)) {
        throw "Retired runtime payload is still present in the ARM64 RuntimeBundle: $retired"
    }
}

if ([string]$manifest.pi.package -ne '@earendil-works/pi-coding-agent') {
    throw 'RuntimeBundle Pi package is not the approved package.'
}

Write-Host 'TuringDesk ARM64 RuntimeBundle verified.' -ForegroundColor Green
Write-Host "goz:  $($manifest.goz.version) ($($manifest.goz.tag))" -ForegroundColor DarkGray
Write-Host "Pi:   $($manifest.pi.version) / $($manifest.pi.mode)" -ForegroundColor DarkGray
