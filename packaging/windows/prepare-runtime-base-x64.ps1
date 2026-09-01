param([Parameter(Mandatory = $true)][string]$DeployDir)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$ProgressPreference = 'SilentlyContinue'

$RepoRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$BundleRoot = Join-Path $RepoRoot 'runtime\x64'
$LockPath = Join-Path $BundleRoot 'runtime-lock.json'

function Sha256([string]$Path) {
    (Get-FileHash -Algorithm SHA256 -Path $Path).Hash.ToLowerInvariant()
}

function Resolve-Archive([string]$Folder,[string]$Name) {
    if ([string]::IsNullOrWhiteSpace($Name)) { throw 'Runtime lock contains an empty archive name.' }
    $path = Join-Path $BundleRoot (Join-Path $Folder $Name)
    if (-not (Test-Path $path -PathType Leaf)) { throw "Runtime archive is missing: $path" }
    $path
}

function Assert-Hash([string]$Path,[string]$Expected) {
    $actual = Sha256 $Path
    if ([string]::IsNullOrWhiteSpace($Expected) -or $actual -ne $Expected.ToLowerInvariant()) {
        throw "Runtime integrity check failed: $Path`nExpected: $Expected`nActual:   $actual"
    }
}

function Expand-Tar([string]$Archive,[string]$Destination) {
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    & tar.exe -xf $Archive -C $Destination
    if ($LASTEXITCODE -ne 0) { throw "Unable to extract Runtime archive: $Archive" }
}

if (-not (Test-Path $LockPath -PathType Leaf)) { throw "Pinned x64 Runtime lock is missing: $LockPath" }
$lock = Get-Content $LockPath -Raw | ConvertFrom-Json
if ([string]$lock.architecture -ne 'x64') { throw 'Pinned Runtime lock is not x64.' }

$nodeArchive = Resolve-Archive 'node' ([string]$lock.node.archive)
$gozArchive = Resolve-Archive 'goz' ([string]$lock.goz.archive)
Assert-Hash $nodeArchive ([string]$lock.node.sha256)
Assert-Hash $gozArchive ([string]$lock.goz.sha256)

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
