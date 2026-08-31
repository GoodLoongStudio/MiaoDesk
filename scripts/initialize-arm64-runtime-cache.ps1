$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$RepoRoot = Split-Path $PSScriptRoot -Parent
$UserDataRoot = Join-Path $env:LOCALAPPDATA 'MiaoDesk'
$RuntimeCacheRoot = Join-Path $UserDataRoot 'RuntimeCache'
$NativeTestRoot = Join-Path $UserDataRoot 'NativeTest'
$DevPreviewRoot = Join-Path $UserDataRoot 'DevPreview'
$BundleRoot = Join-Path $RepoRoot 'runtime\arm64'
$ManifestPath = Join-Path $BundleRoot 'runtime-manifest.json'
$CompleteMarker = Join-Path $BundleRoot '.complete'
$WallpaperSource = Join-Path $RepoRoot 'assets\wallpapers'

function Step([string]$Text) { Write-Host "`n==> $Text" -ForegroundColor Cyan }
function Warn([string]$Text) { Write-Host $Text -ForegroundColor Yellow }
function Stop-MiaoDeskProcesses {
    foreach ($name in @('MiaoDesk', 'MiaoDeskWallpaper', 'MiaoDeskHarness', 'node')) {
        Get-Process $name -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    }
    Start-Sleep -Milliseconds 250
}
function Remove-TreeRobust([string]$Path) {
    if (-not (Test-Path $Path)) { return }
    $empty = Join-Path $env:TEMP ("mdp-empty-" + [Guid]::NewGuid().ToString('N').Substring(0, 8))
    New-Item -ItemType Directory -Force -Path $empty | Out-Null
    try {
        & robocopy.exe $empty $Path /MIR /R:1 /W:1 /NFL /NDL /NJH /NJS /NP /XJ | Out-Null
    }
    finally {
        Remove-Item $empty -Force -ErrorAction SilentlyContinue
    }
    Remove-Item $Path -Recurse -Force -ErrorAction SilentlyContinue
    if (Test-Path $Path) { & cmd.exe /d /c "rd /s /q `"$Path`"" | Out-Null }
}
function Mirror-TreeRobust([string]$Source, [string]$Destination) {
    if (-not (Test-Path $Source -PathType Container)) { return $false }
    if (Test-Path $Destination) { Remove-TreeRobust $Destination }
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    & robocopy.exe $Source $Destination /MIR /COPY:DAT /DCOPY:DAT /R:2 /W:1 /NFL /NDL /NJH /NJS /NP /XJ | Out-Null
    $copyExit = $LASTEXITCODE
    if ($copyExit -ge 8) {
        throw "Runtime cache copy failed with robocopy exit code ${copyExit}: $Source"
    }
    return $true
}
function Test-RuntimeReady([string]$Root) {
    if ([string]::IsNullOrWhiteSpace($Root)) { return $false }
    $node = Join-Path $Root 'Runtime\Node\node.exe'
    $dsh = Join-Path $Root 'Runtime\Node\node_modules\@deepseek-ai\dsh\lib\bin.js'
    $pi = Join-Path $Root 'Pi\node_modules\@earendil-works\pi-coding-agent\dist\cli.js'
    return (Test-Path $node -PathType Leaf) -and
           (Test-Path $dsh -PathType Leaf) -and
           (Test-Path $pi -PathType Leaf)
}
function Resolve-BundleFile([string]$Relative) {
    if ([string]::IsNullOrWhiteSpace($Relative)) { throw 'Runtime manifest contains an empty archive path.' }
    $path = Join-Path $BundleRoot ($Relative -replace '/', '\')
    if (-not (Test-Path $path -PathType Leaf)) {
        throw "Local ARM64 RuntimeBundle file is missing: $path"
    }
    if ((Get-Item $path).Length -lt 1024) {
        throw "Local ARM64 RuntimeBundle file looks like an unresolved Git LFS pointer: $path`nRun git lfs pull once, then rerun this initializer."
    }
    return $path
}
function Expand-Bundle([string]$Archive, [string]$Destination) {
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    & tar.exe -xf $Archive -C $Destination
    $tarExit = $LASTEXITCODE
    if ($tarExit -ne 0) { throw "Failed to extract local runtime archive (tar=${tarExit}): $Archive" }
}
function Seed-FromExisting([string]$SourceRoot) {
    if (-not (Test-RuntimeReady $SourceRoot)) { return $false }
    Step "Reusing existing local runtime from $SourceRoot"
    Stop-MiaoDeskProcesses
    New-Item -ItemType Directory -Force -Path $RuntimeCacheRoot | Out-Null
    foreach ($name in @('Runtime', 'Pi', 'Goz', 'Wallpapers', 'Assets')) {
        $source = Join-Path $SourceRoot $name
        if (Test-Path $source -PathType Container) {
            [void](Mirror-TreeRobust $source (Join-Path $RuntimeCacheRoot $name))
        }
    }
    return (Test-RuntimeReady $RuntimeCacheRoot)
}
function Validate-Cache {
    $node = Join-Path $RuntimeCacheRoot 'Runtime\Node\node.exe'
    $dsh = Join-Path $RuntimeCacheRoot 'Runtime\Node\node_modules\@deepseek-ai\dsh\lib\bin.js'
    $pi = Join-Path $RuntimeCacheRoot 'Pi\node_modules\@earendil-works\pi-coding-agent\dist\cli.js'
    foreach ($item in @($node, $dsh, $pi)) {
        if (-not (Test-Path $item -PathType Leaf)) { throw "RuntimeCache is incomplete: $item" }
    }

    & $node $pi --version | Out-Host
    if ($LASTEXITCODE -ne 0) { throw 'Cached Pi Agent runtime failed its local version probe.' }
}

foreach ($required in @('robocopy.exe', 'tar.exe', 'git')) {
    if (-not (Get-Command $required -ErrorAction SilentlyContinue)) { throw "$required is required." }
}
Set-Location $RepoRoot

if (Test-RuntimeReady $RuntimeCacheRoot) {
    Step 'Persistent RuntimeCache is already ready'
    Validate-Cache
    Write-Host "RuntimeCache: $RuntimeCacheRoot" -ForegroundColor Green
    exit 0
}

foreach ($candidate in @($NativeTestRoot, $DevPreviewRoot)) {
    if (Seed-FromExisting $candidate) {
        Validate-Cache
        Set-Content -Path (Join-Path $RuntimeCacheRoot 'runtime-cache-source.txt') -Value $candidate -Encoding UTF8
        Write-Host "Persistent RuntimeCache seeded without any download: $RuntimeCacheRoot" -ForegroundColor Green
        exit 0
    }
}

Step 'Seeding RuntimeCache from repository-local ARM64 RuntimeBundle (offline)'
if (-not (Test-Path $ManifestPath -PathType Leaf)) {
    throw "Repository ARM64 RuntimeBundle manifest is missing: $ManifestPath"
}
if (-not (Test-Path $CompleteMarker -PathType Leaf)) {
    Warn "RuntimeBundle .complete marker is missing: $CompleteMarker"
}

$manifest = Get-Content $ManifestPath -Raw | ConvertFrom-Json
if ([string]$manifest.architecture -ne 'arm64' -or [int]$manifest.schema -lt 2) {
    throw 'Repository RuntimeBundle architecture/schema mismatch.'
}

$nodeArchive = Resolve-BundleFile ([string]$manifest.node.archive)
$harnessArchive = Resolve-BundleFile ([string]$manifest.deepseekHarness.archive)
$piArchive = Resolve-BundleFile ([string]$manifest.pi.archive)
$gozArchive = Resolve-BundleFile ([string]$manifest.goz.archive)

Stop-MiaoDeskProcesses
if (Test-Path $RuntimeCacheRoot) { Remove-TreeRobust $RuntimeCacheRoot }
New-Item -ItemType Directory -Force -Path $RuntimeCacheRoot | Out-Null

$nodeTemp = Join-Path $env:TEMP ('mdp-node-' + [Guid]::NewGuid().ToString('N').Substring(0, 8))
try {
    Expand-Bundle $nodeArchive $nodeTemp
    $nodeRoot = Get-ChildItem $nodeTemp -Directory | Where-Object {
        Test-Path (Join-Path $_.FullName 'node.exe') -PathType Leaf
    } | Select-Object -First 1
    if (-not $nodeRoot) { throw 'Repository Node ARM64 archive does not contain node.exe.' }

    $runtimeNode = Join-Path $RuntimeCacheRoot 'Runtime\Node'
    [void](Mirror-TreeRobust $nodeRoot.FullName $runtimeNode)
    Expand-Bundle $harnessArchive $runtimeNode
}
finally {
    Remove-TreeRobust $nodeTemp
}

Expand-Bundle $piArchive (Join-Path $RuntimeCacheRoot 'Pi')
Expand-Bundle $gozArchive (Join-Path $RuntimeCacheRoot 'Goz')

if (Test-Path $WallpaperSource -PathType Container) {
    [void](Mirror-TreeRobust $WallpaperSource (Join-Path $RuntimeCacheRoot 'Wallpapers'))
}

Copy-Item $ManifestPath (Join-Path $RuntimeCacheRoot 'runtime-manifest.json') -Force
$headSha = (& git rev-parse HEAD).Trim()
Set-Content -Path (Join-Path $RuntimeCacheRoot 'runtime-cache-source-sha.txt') -Value $headSha -Encoding ASCII
Set-Content -Path (Join-Path $RuntimeCacheRoot 'runtime-cache-source.txt') -Value 'repository-local runtime/arm64' -Encoding UTF8

Validate-Cache

$goz = Join-Path $RuntimeCacheRoot 'Goz\goz.exe'
$gozd = Join-Path $RuntimeCacheRoot 'Goz\gozd.exe'
if ((Test-Path $goz -PathType Leaf) -and (Test-Path $gozd -PathType Leaf)) {
    Write-Host 'Goz binaries cached locally. File-index service installation is kept separate from fast UI preview startup.' -ForegroundColor DarkGray
}

Write-Host "Persistent RuntimeCache ready: $RuntimeCacheRoot" -ForegroundColor Green
Write-Host 'Source: repository-local runtime/arm64 (no GitHub Actions artifact download).' -ForegroundColor Green
Write-Host 'Future PREVIEW-MIAODESK-ARM64.cmd runs only download UI EXEs/DLLs and mount this cache.' -ForegroundColor Green
