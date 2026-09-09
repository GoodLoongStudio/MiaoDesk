param(
    [Parameter(Mandatory = $true)][string]$Destination,
    [Parameter(Mandatory = $true)][ValidateSet('x64','arm64')][string]$Architecture
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Assert-File([string]$Relative) {
    $path = Join-Path $Destination $Relative
    if (-not (Test-Path $path -PathType Leaf)) { throw "Package staging is missing: $Relative" }
}

foreach ($relative in @(
    'MiaoDesk.exe',
    'MiaoDeskWallpaper.exe',
    'MiaoDeskHarness.exe',
    'Assets\MiaoMiao.ico',
    'Wallpapers\MiaoCloud.mdwall\manifest.json'
)) { Assert-File $relative }

& (Join-Path $PSScriptRoot 'prepare-runtime-base.ps1') -DeployDir $Destination -Architecture $Architecture
if ($LASTEXITCODE -ne 0) { throw "Base Runtime staging failed with exit code $LASTEXITCODE" }

& (Join-Path $PSScriptRoot 'build-agent-runtime.ps1') -Root $Destination -Architecture $Architecture
if ($LASTEXITCODE -ne 0) { throw "Agent Runtime staging failed with exit code $LASTEXITCODE" }

foreach ($relative in @(
    'Runtime\Node\node.exe',
    'AI\package.json',
    'AI\package-lock.json',
    'AI\node_modules\@deepseek-ai\dsh\lib\bin.js',
    'AI\node_modules\pi\dist\cli.js',
    'Goz\goz.exe',
    'Goz\gozd.exe',
    'Wallpapers\MiaoCloud.mdwall\assets\background.jpg',
    'Wallpapers\MiaoCloud.mdwall\assets\cat.png',
    'Wallpapers\MiaoCloud.mdwall\assets\tail.png',
    'Wallpapers\MiaoCloud.mdwall\assets\blink.png',
    'Wallpapers\NeonCity.mdwall\manifest.json',
    'Wallpapers\MysticMoon.mdwall\manifest.json'
)) { Assert-File $relative }

foreach ($relative in @(
    'Wallpapers\MiaoCloud.mdwall\assets\background.jpg',
    'Wallpapers\MiaoCloud.mdwall\assets\cat.png',
    'Wallpapers\MiaoCloud.mdwall\assets\tail.png',
    'Wallpapers\MiaoCloud.mdwall\assets\blink.png'
)) {
    if ((Get-Item (Join-Path $Destination $relative)).Length -lt 1024) {
        throw "Wallpaper image still looks like an LFS pointer: $relative"
    }
}

if (Test-Path (Join-Path $Destination 'node_modules')) {
    throw 'Install-root node_modules must not exist.'
}

Write-Host "Windows $Architecture package staging ready: $Destination" -ForegroundColor Green
