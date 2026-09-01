$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path $PSScriptRoot -Parent
$BundleRoot = Join-Path $RepoRoot 'runtime\arm64'
$LockPath = Join-Path $BundleRoot 'runtime-lock.json'
$ManifestPath = Join-Path $BundleRoot 'runtime-manifest.json'
$CompleteMarker = Join-Path $BundleRoot '.complete'
$WebViewManifestPath = Join-Path $RepoRoot 'third_party\webview2\manifest.json'

function Sha256([string]$Path) {
    (Get-FileHash -Algorithm SHA256 -Path $Path).Hash.ToLowerInvariant()
}
function Resolve-BundleFile([string]$RelativePath) {
    $path = Join-Path $BundleRoot ($RelativePath -replace '/', '\')
    if (-not (Test-Path $path -PathType Leaf)) { throw "Vendored runtime file is missing: $path" }
    $path
}
function Assert-Hash([string]$Path, [string]$Expected) {
    $actual = Sha256 $Path
    if ([string]::IsNullOrWhiteSpace($Expected) -or $actual -ne $Expected.ToLowerInvariant()) {
        throw "Hash mismatch: $Path`nExpected: $Expected`nActual:   $actual"
    }
}

foreach ($required in @($LockPath,$ManifestPath,$CompleteMarker,$WebViewManifestPath)) {
    if (-not (Test-Path $required -PathType Leaf)) { throw "ARM64 RuntimeBundle prerequisite is missing: $required" }
}

$lock = Get-Content $LockPath -Raw | ConvertFrom-Json
$manifest = Get-Content $ManifestPath -Raw | ConvertFrom-Json
if ($lock.architecture -ne 'arm64' -or $manifest.architecture -ne 'arm64') { throw 'RuntimeBundle architecture is not ARM64.' }
if ($manifest.lockSha256.ToLowerInvariant() -ne (Sha256 $LockPath)) { throw 'RuntimeBundle is stale relative to runtime-lock.json.' }

Assert-Hash (Resolve-BundleFile ([string]$manifest.node.archive)) ([string]$manifest.node.sha256)
Assert-Hash (Resolve-BundleFile ([string]$manifest.deepseekHarness.archive)) ([string]$manifest.deepseekHarness.sha256)
Assert-Hash (Resolve-BundleFile ([string]$manifest.goz.archive)) ([string]$manifest.goz.sha256)
Assert-Hash (Resolve-BundleFile ([string]$manifest.pi.archive)) ([string]$manifest.pi.sha256)

$webView = Get-Content $WebViewManifestPath -Raw | ConvertFrom-Json
$loader = Join-Path $RepoRoot ("third_party\webview2\{0}\build\native\arm64\WebView2LoaderStatic.lib" -f [string]$webView.version)
Assert-Hash $loader ([string]$webView.loaders.arm64)

foreach ($retired in @('everything','codex','codex-relay','webview2-sdk')) {
    if (Test-Path (Join-Path $BundleRoot $retired)) { throw "Non-runtime payload is still present in runtime/arm64: $retired" }
}

if ([string]$manifest.pi.package -ne '@earendil-works/pi-coding-agent') { throw 'RuntimeBundle Pi package is not approved.' }
Write-Host 'MiaoDesk ARM64 RuntimeBundle verified.' -ForegroundColor Green
