param([Parameter(Mandatory = $true)][string]$DeployDir)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$ProgressPreference = 'SilentlyContinue'

$RepoRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$BundleRoot = Join-Path $RepoRoot 'runtime\x64'
$ManifestPath = Join-Path $BundleRoot 'runtime-manifest.json'
$CompleteMarker = Join-Path $BundleRoot '.complete'

function Sha256([string]$Path) {
    (Get-FileHash -Algorithm SHA256 -Path $Path).Hash.ToLowerInvariant()
}

function Resolve-BundleFile([string]$RelativePath) {
    if ([string]::IsNullOrWhiteSpace($RelativePath)) { throw 'Runtime manifest contains an empty archive path.' }
    $path = Join-Path $BundleRoot ($RelativePath -replace '/', '\')
    if (-not (Test-Path $path -PathType Leaf)) { throw "Runtime archive is missing: $path" }
    $path
}

function Assert-Hash([string]$Path, [string]$Expected) {
    $actual = Sha256 $Path
    if ([string]::IsNullOrWhiteSpace($Expected) -or $actual -ne $Expected.ToLowerInvariant()) {
        throw "Runtime integrity check failed: $Path`nExpected: $Expected`nActual:   $actual"
    }
}

function Expand-Tar([string]$Archive, [string]$Destination) {
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    & tar.exe -xf $Archive -C $Destination
    if ($LASTEXITCODE -ne 0) { throw "Unable to extract Runtime archive: $Archive" }
}

if (-not (Test-Path $ManifestPath -PathType Leaf) -or -not (Test-Path $CompleteMarker -PathType Leaf)) {
    throw 'Pinned x64 Runtime metadata is incomplete.'
}
$manifest = Get-Content $ManifestPath -Raw | ConvertFrom-Json
if ([string]$manifest.architecture -ne 'x64' -or [int]$manifest.schema -lt 2) {
    throw 'Pinned x64 Runtime manifest is incompatible.'
}

$nodeArchive = Resolve-BundleFile ([string]$manifest.node.archive)
$gozArchive = Resolve-BundleFile ([string]$manifest.goz.archive)
Assert-Hash $nodeArchive ([string]$manifest.node.sha256)
Assert-Hash $gozArchive ([string]$manifest.goz.sha256)

$runtimeDir = Join-Path $DeployDir 'Runtime'
$nodeDir = Join-Path $runtimeDir 'Node'
$gozDir = Join-Path $DeployDir 'Goz'
Remove-Item $nodeDir,$gozDir -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $runtimeDir | Out-Null

$tempBase = if ([string]::IsNullOrWhiteSpace($env:RUNNER_TEMP)) { $env:TEMP } else { $env:RUNNER_TEMP }
$nodeTemp = Join-Path $tempBase ('MiaoDesk-Node-' + [guid]::NewGuid().ToString('N'))
$gozTemp = Join-Path $tempBase ('MiaoDesk-Goz-' + [guid]::NewGuid().ToString('N'))
try {
    Expand-Tar $nodeArchive $nodeTemp
    $nodeHome = Get-ChildItem $nodeTemp -Directory -Recurse |
        Where-Object { Test-Path (Join-Path $_.FullName 'node.exe') -PathType Leaf } |
        Select-Object -First 1
    if (-not $nodeHome) { throw 'Pinned Node archive does not contain node.exe.' }
    New-Item -ItemType Directory -Force -Path $nodeDir | Out-Null
    Copy-Item (Join-Path $nodeHome.FullName '*') $nodeDir -Recurse -Force

    Expand-Tar $gozArchive $gozTemp
    $goz = Get-ChildItem $gozTemp -Filter 'goz.exe' -File -Recurse | Select-Object -First 1
    $gozd = Get-ChildItem $gozTemp -Filter 'gozd.exe' -File -Recurse | Select-Object -First 1
    if (-not $goz -or -not $gozd) { throw 'Pinned goz archive is incomplete.' }
    New-Item -ItemType Directory -Force -Path $gozDir | Out-Null
    Copy-Item $goz.FullName (Join-Path $gozDir 'goz.exe') -Force
    Copy-Item $gozd.FullName (Join-Path $gozDir 'gozd.exe') -Force
} finally {
    Remove-Item $nodeTemp,$gozTemp -Recurse -Force -ErrorAction SilentlyContinue
}

$nodeExe = Join-Path $nodeDir 'node.exe'
$npmCmd = Join-Path $nodeDir 'npm.cmd'
foreach ($required in @($nodeExe,$npmCmd,(Join-Path $gozDir 'goz.exe'),(Join-Path $gozDir 'gozd.exe'))) {
    if (-not (Test-Path $required -PathType Leaf)) { throw "Runtime staging is incomplete: $required" }
}

& $nodeExe --version | Out-Host
if ($LASTEXITCODE -ne 0) { throw 'Bundled Node probe failed.' }

Write-Host 'x64 base Runtime ready: Node + npm + goz.' -ForegroundColor Green
