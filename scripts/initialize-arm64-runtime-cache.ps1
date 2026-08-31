$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$RepoRoot = Split-Path $PSScriptRoot -Parent
$UserDataRoot = Join-Path $env:LOCALAPPDATA 'MiaoDesk'
$PreviewRoot = Join-Path $UserDataRoot 'DevPreview'
$RuntimeCacheRoot = Join-Path $UserDataRoot 'RuntimeCache'
$FullPreviewScript = Join-Path $PSScriptRoot 'download-arm64-preview.ps1'

function Step([string]$Text) { Write-Host "`n==> $Text" -ForegroundColor Cyan }
function Stop-MiaoDeskProcesses {
    foreach ($name in @('MiaoDesk', 'MiaoDeskWallpaper', 'MiaoDeskHarness')) {
        Get-Process $name -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    }
    Start-Sleep -Milliseconds 300
}
function Mirror-TreeRobust([string]$Source, [string]$Destination) {
    if (-not (Test-Path $Source -PathType Container)) { return $false }
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    & robocopy.exe $Source $Destination /MIR /COPY:DAT /DCOPY:DAT /R:2 /W:1 /NFL /NDL /NJH /NJS /NP /XJ | Out-Null
    if ($LASTEXITCODE -ge 8) {
        throw "Runtime cache copy failed with robocopy exit code $LASTEXITCODE: $Source"
    }
    return $true
}

if (-not (Test-Path $FullPreviewScript -PathType Leaf)) {
    throw "Full preview downloader is missing: $FullPreviewScript"
}
if (-not (Get-Command robocopy.exe -ErrorAction SilentlyContinue)) {
    throw 'robocopy.exe is required.'
}

Set-Location $RepoRoot

Step 'Downloading one full ARM64 preview to seed the local runtime cache'
& $FullPreviewScript

if (-not (Test-Path $PreviewRoot -PathType Container)) {
    throw "Full preview did not create: $PreviewRoot"
}

Step 'Persisting bundled runtime for future FAST previews'
Stop-MiaoDeskProcesses
New-Item -ItemType Directory -Force -Path $RuntimeCacheRoot | Out-Null

$cached = New-Object System.Collections.Generic.List[string]
foreach ($name in @('Runtime', 'Pi', 'Goz', 'Wallpapers', 'Assets')) {
    $source = Join-Path $PreviewRoot $name
    $destination = Join-Path $RuntimeCacheRoot $name
    if (Mirror-TreeRobust $source $destination) { [void]$cached.Add($name) }
}

$node = Join-Path $RuntimeCacheRoot 'Runtime\Node\node.exe'
$pi = Join-Path $RuntimeCacheRoot 'Pi\node_modules\@earendil-works\pi-coding-agent\dist\cli.js'
if (-not (Test-Path $node -PathType Leaf)) {
    throw "Runtime cache is incomplete: bundled Node is missing at $node"
}
if (-not (Test-Path $pi -PathType Leaf)) {
    throw "Runtime cache is incomplete: Pi CLI is missing at $pi"
}

$headSha = (& git rev-parse HEAD).Trim()
Set-Content -Path (Join-Path $RuntimeCacheRoot 'runtime-cache-source-sha.txt') -Value $headSha -Encoding ASCII
Set-Content -Path (Join-Path $RuntimeCacheRoot 'runtime-cache-components.txt') -Value ($cached -join "`r`n") -Encoding ASCII

Write-Host ("Persistent RuntimeCache ready: " + $RuntimeCacheRoot) -ForegroundColor Green
Write-Host ("Cached components: " + ($cached -join ', ')) -ForegroundColor Green
Write-Host 'Future PREVIEW-MIAODESK-ARM64.cmd runs download only UI executables/DLLs and reuse this cache.' -ForegroundColor Green

$exe = Join-Path $PreviewRoot 'MiaoDesk.exe'
if (Test-Path $exe -PathType Leaf) {
    Step 'Restarting the full preview'
    Start-Process -FilePath $exe -WorkingDirectory $PreviewRoot
}
