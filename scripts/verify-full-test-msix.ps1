param(
    [Parameter(Mandatory = $true)][string]$PackagePath,
    [Parameter(Mandatory = $true)][string]$Destination
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$sdkBin = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
$makeAppx = Get-ChildItem $sdkBin -Recurse -Filter MakeAppx.exe -File |
    Where-Object { (Split-Path $_.DirectoryName -Leaf) -in @('x64','arm64') } |
    Sort-Object FullName -Descending | Select-Object -First 1
if (-not $makeAppx) { throw 'MakeAppx.exe was not found.' }

Remove-Item $Destination -Recurse -Force -ErrorAction SilentlyContinue
& $makeAppx.FullName unpack /p $PackagePath /d $Destination /o
if ($LASTEXITCODE -ne 0) { throw "MakeAppx unpack failed with exit code $LASTEXITCODE" }

$required = @(
    'MiaoDesk.exe',
    'MiaoDeskWallpaper.exe',
    'MiaoDeskHarness.exe',
    'Runtime\Node\node.exe',
    'Runtime\Node\node_modules\@deepseek-ai\dsh\lib\bin.js',
    'Pi\node_modules\@earendil-works\pi-coding-agent\dist\cli.js',
    'Goz\goz.exe',
    'Goz\gozd.exe',
    'Wallpapers\MiaoCloud.mdwall\manifest.json',
    'Wallpapers\MiaoCloud.mdwall\scene.ini',
    'Wallpapers\MiaoCloud.mdwall\assets\cat.png',
    'Wallpapers\NeonCity.mdwall\manifest.json',
    'Wallpapers\NeonCity.mdwall\scene.ini',
    'Wallpapers\MysticMoon.mdwall\manifest.json',
    'Wallpapers\MysticMoon.mdwall\scene.ini',
    'Assets\StoreLogo.png',
    'Assets\Square44x44Logo.png',
    'Assets\Square150x150Logo.png',
    'Assets\Wide310x150Logo.png',
    'Assets\MiaoMiao.ico',
    'AppxManifest.xml'
)
foreach ($relative in $required) {
    if (-not (Test-Path (Join-Path $Destination $relative) -PathType Leaf)) {
        throw "MSIX payload is missing: $relative"
    }
}

if ((Get-Item (Join-Path $Destination 'Wallpapers\MiaoCloud.mdwall\assets\cat.png')).Length -lt 1024) {
    throw 'Packaged MiaoCloud cat.png still looks like a Git LFS pointer.'
}

Write-Host "MSIX payload verification passed: $PackagePath" -ForegroundColor Green
