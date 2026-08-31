param(
    [Parameter(Mandatory = $true)][string]$DeployDir,
    [Parameter(Mandatory = $true)][ValidateSet('arm64', 'x64')][string]$Architecture
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
Set-StrictMode -Version Latest

$RepoRoot = Split-Path $PSScriptRoot -Parent

function Sha256([string]$Path) {
    (Get-FileHash -Algorithm SHA256 -Path $Path).Hash.ToLowerInvariant()
}

function Assert-File([string]$Path, [string]$Label) {
    if (-not (Test-Path $Path -PathType Leaf)) {
        throw ("Missing {0}: {1}" -f $Label, $Path)
    }
}

function Download-Pinned([string]$Url, [string]$Path, [string]$ExpectedSha256) {
    Write-Host "Downloading $Url"
    Invoke-WebRequest -Uri $Url -OutFile $Path -UseBasicParsing
    Assert-File $Path 'downloaded runtime archive'
    $actual = Sha256 $Path
    if ([string]::IsNullOrWhiteSpace($ExpectedSha256) -or $actual -ne $ExpectedSha256.ToLowerInvariant()) {
        throw "Runtime archive checksum mismatch: $Path`nExpected: $ExpectedSha256`nActual:   $actual"
    }
}

function Expand-Zip([string]$Archive, [string]$Destination) {
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    Expand-Archive -Path $Archive -DestinationPath $Destination -Force
}

function Test-CompleteRuntime([string]$Root) {
    $required = @(
        (Join-Path $Root 'Runtime\Node\node.exe'),
        (Join-Path $Root 'Runtime\Node\node_modules\@deepseek-ai\dsh\lib\bin.js'),
        (Join-Path $Root 'Pi\node_modules\@earendil-works\pi-coding-agent\dist\cli.js'),
        (Join-Path $Root 'Goz\goz.exe'),
        (Join-Path $Root 'Goz\gozd.exe')
    )
    foreach ($path in $required) {
        if (-not (Test-Path $path -PathType Leaf)) { return $false }
    }
    return $true
}

New-Item -ItemType Directory -Force -Path $DeployDir | Out-Null

if ($Architecture -eq 'arm64') {
    $arm64Installer = Join-Path $PSScriptRoot 'prepare-third-party-runtime-arm64.ps1'
    Assert-File $arm64Installer 'ARM64 RuntimeBundle installer'
    & $arm64Installer -DeployDir $DeployDir -SkipGozServiceInstall
    if ($LASTEXITCODE -ne 0) { throw "ARM64 RuntimeBundle installer failed with exit code $LASTEXITCODE" }

    if (-not (Test-CompleteRuntime $DeployDir)) {
        throw 'ARM64 Store runtime is incomplete after offline materialization.'
    }
    Write-Host 'Complete ARM64 Store runtime is ready.' -ForegroundColor Green
    exit 0
}

$lockPath = Join-Path $RepoRoot 'runtime\x64\runtime-lock.json'
Assert-File $lockPath 'x64 runtime lock'
$lock = Get-Content $lockPath -Raw | ConvertFrom-Json
if ($lock.architecture -ne 'x64' -or [int]$lock.schema -lt 2) {
    throw 'x64 runtime lock architecture/schema mismatch.'
}

$stage = Join-Path $env:RUNNER_TEMP ('MiaoDesk-StoreRuntime-x64-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $stage | Out-Null
try {
    $RuntimeDir = Join-Path $DeployDir 'Runtime'
    $NodeDir = Join-Path $RuntimeDir 'Node'
    $PiDir = Join-Path $DeployDir 'Pi'
    $GozDir = Join-Path $DeployDir 'Goz'
    Remove-Item $NodeDir,$PiDir,$GozDir -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path $RuntimeDir,$PiDir,$GozDir | Out-Null

    # Portable Node x64. The exact archive hash is pinned in runtime/x64/runtime-lock.json.
    $nodeArchive = Join-Path $stage ([string]$lock.node.archive)
    $nodeUrl = ([string]$lock.node.source).TrimEnd('/') + '/' + [string]$lock.node.archive
    Download-Pinned $nodeUrl $nodeArchive ([string]$lock.node.sha256)
    $nodeExpanded = Join-Path $stage 'node-expanded'
    Expand-Zip $nodeArchive $nodeExpanded
    $nodeRoot = Get-ChildItem $nodeExpanded -Directory | Where-Object {
        Test-Path (Join-Path $_.FullName 'node.exe') -PathType Leaf
    } | Select-Object -First 1
    if (-not $nodeRoot) { throw 'Portable Node x64 archive does not contain node.exe.' }
    New-Item -ItemType Directory -Force -Path $NodeDir | Out-Null
    Copy-Item (Join-Path $nodeRoot.FullName '*') $NodeDir -Recurse -Force

    $nodeExe = Join-Path $NodeDir 'node.exe'
    $npmCmd = Join-Path $NodeDir 'npm.cmd'
    Assert-File $nodeExe 'bundled x64 Node runtime'
    Assert-File $npmCmd 'bundled x64 npm'

    $env:npm_config_cache = Join-Path $stage 'npm-cache'

    # DeepSeek Harness: install its production dependency tree next to the shared Node runtime.
    $harnessStage = Join-Path $stage 'harness'
    New-Item -ItemType Directory -Force -Path $harnessStage | Out-Null
    @{ name = 'miaodesk-deepseek-harness-runtime'; private = $true; version = '1.0.0' } |
        ConvertTo-Json | Set-Content (Join-Path $harnessStage 'package.json') -Encoding UTF8
    & $npmCmd install --prefix $harnessStage (([string]$lock.deepseekHarness.package) + '@' + ([string]$lock.deepseekHarness.version)) --omit=dev --no-audit --no-fund --save-exact
    if ($LASTEXITCODE -ne 0) { throw 'DeepSeek Harness x64 production install failed.' }
    Copy-Item (Join-Path $harnessStage 'node_modules') $NodeDir -Recurse -Force

    # Pi remains isolated from Harness dependencies while sharing the same Node executable.
    @{ name = 'miaodesk-pi-runtime'; private = $true; version = '1.0.0' } |
        ConvertTo-Json | Set-Content (Join-Path $PiDir 'package.json') -Encoding UTF8
    & $npmCmd install --prefix $PiDir (([string]$lock.pi.package) + '@' + ([string]$lock.pi.version)) --omit=dev --no-audit --no-fund --save-exact
    if ($LASTEXITCODE -ne 0) { throw 'Pi x64 production install failed.' }

    # goz provides the native NTFS/MFT search client and elevated indexing daemon.
    $gozArchive = Join-Path $stage ([string]$lock.goz.archive)
    Download-Pinned ([string]$lock.goz.source) $gozArchive ([string]$lock.goz.sha256)
    $gozExpanded = Join-Path $stage 'goz-expanded'
    Expand-Zip $gozArchive $gozExpanded
    $gozExe = Get-ChildItem $gozExpanded -Filter 'goz.exe' -File -Recurse | Select-Object -First 1
    $gozdExe = Get-ChildItem $gozExpanded -Filter 'gozd.exe' -File -Recurse | Select-Object -First 1
    if (-not $gozExe -or -not $gozdExe) { throw 'goz x64 archive is incomplete.' }
    Copy-Item (Join-Path $gozExe.Directory.FullName '*') $GozDir -Recurse -Force
    if (-not (Test-Path (Join-Path $GozDir 'gozd.exe') -PathType Leaf) -and $gozdExe.Directory.FullName -ne $gozExe.Directory.FullName) {
        Copy-Item $gozdExe.FullName (Join-Path $GozDir 'gozd.exe') -Force
    }

    $dshBin = Join-Path $NodeDir 'node_modules\@deepseek-ai\dsh\lib\bin.js'
    $piCli = Join-Path $PiDir 'node_modules\@earendil-works\pi-coding-agent\dist\cli.js'
    $gozClient = Join-Path $GozDir 'goz.exe'
    $gozDaemon = Join-Path $GozDir 'gozd.exe'
    foreach ($item in @($nodeExe,$dshBin,$piCli,$gozClient,$gozDaemon)) {
        Assert-File $item 'complete x64 runtime component'
    }

    & $nodeExe --version | Out-Host
    if ($LASTEXITCODE -ne 0) { throw 'Bundled Node x64 failed to run.' }
    & $nodeExe $dshBin --help | Out-Host
    if ($LASTEXITCODE -ne 0) { throw 'DeepSeek Harness x64 failed to run.' }
    & $nodeExe $piCli --version | Out-Host
    if ($LASTEXITCODE -ne 0) { throw 'Pi x64 failed to run.' }
    & $gozDaemon status | Out-Host
    # A non-zero status is expected before service installation; binary launch itself is what matters here.

    $runtimeManifest = [ordered]@{
        schema = 1
        architecture = 'x64'
        node = [string]$lock.node.version
        deepseekHarness = [string]$lock.deepseekHarness.version
        pi = [string]$lock.pi.version
        goz = [string]$lock.goz.version
        materializedUtc = [DateTime]::UtcNow.ToString('o')
    }
    $runtimeManifest | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $RuntimeDir 'store-runtime-manifest.json') -Encoding UTF8

    if (-not (Test-CompleteRuntime $DeployDir)) {
        throw 'x64 Store runtime is incomplete after materialization.'
    }
    Write-Host 'Complete x64 Store runtime is ready.' -ForegroundColor Green
}
finally {
    Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue
}
