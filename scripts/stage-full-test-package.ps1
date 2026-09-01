param(
    [string]$BuildOutput = '',
    [Parameter(Mandatory = $true)][string]$Destination,
    [Parameter(Mandatory = $true)][ValidateSet('arm64','x64')][string]$Architecture,
    [switch]$UseCMakeInstallOutput
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$RepoRoot = Split-Path $PSScriptRoot -Parent

function Assert-File([string]$Root, [string]$Relative) {
    $path = Join-Path $Root $Relative
    if (-not (Test-Path $path -PathType Leaf)) {
        throw "Full test package is missing: $Relative"
    }
}

New-Item -ItemType Directory -Force -Path $Destination | Out-Null

if ($UseCMakeInstallOutput) {
    # Native executables, icon and wallpaper assets must already have been
    # produced by `cmake --install --prefix $Destination`. This keeps the
    # installer/portable package rooted directly at the canonical install tree.
    foreach ($relative in @(
        'MiaoDesk.exe',
        'MiaoDeskWallpaper.exe',
        'MiaoDeskHarness.exe',
        'Assets\MiaoMiao.ico',
        'Wallpapers\MiaoCloud.mdwall\manifest.json'
    )) { Assert-File $Destination $relative }
} else {
    if ([string]::IsNullOrWhiteSpace($BuildOutput)) {
        throw 'BuildOutput is required unless -UseCMakeInstallOutput is specified.'
    }
    foreach ($exe in @('MiaoDesk.exe','MiaoDeskWallpaper.exe','MiaoDeskHarness.exe')) {
        Assert-File $BuildOutput $exe
        Copy-Item (Join-Path $BuildOutput $exe) $Destination -Force
    }
    Get-ChildItem $BuildOutput -Filter '*.dll' -File -ErrorAction SilentlyContinue |
        Copy-Item -Destination $Destination -Force

    $wallpapersSource = Join-Path $RepoRoot 'assets\wallpapers'
    $wallpapersDestination = Join-Path $Destination 'Wallpapers'
    New-Item -ItemType Directory -Force -Path $wallpapersDestination | Out-Null
    Copy-Item (Join-Path $wallpapersSource '*') $wallpapersDestination -Recurse -Force

    $assetsDestination = Join-Path $Destination 'Assets'
    New-Item -ItemType Directory -Force -Path $assetsDestination | Out-Null
    Copy-Item (Join-Path $RepoRoot 'packaging\windows-store\assets\MiaoMiao.ico') $assetsDestination -Force
}

# Only materialize production runtime payloads. Repository SDK/build trees (for
# example WebView2 SDK build metadata) never enter the end-user install tree.
if ($Architecture -eq 'x64' -and
    (Test-Path (Join-Path $RepoRoot 'runtime\x64\runtime-manifest.json') -PathType Leaf) -and
    (Test-Path (Join-Path $RepoRoot 'runtime\x64\.complete') -PathType Leaf)) {
    Write-Host 'Using vendored x64 RuntimeBundle (offline).' -ForegroundColor Cyan
    & (Join-Path $PSScriptRoot 'prepare-third-party-runtime-x64.ps1') -DeployDir $Destination -SkipGozServiceInstall
} else {
    & (Join-Path $PSScriptRoot 'prepare-store-runtime.ps1') -DeployDir $Destination -Architecture $Architecture
}
if ($LASTEXITCODE -ne 0) { throw "Runtime materialization failed with exit code $LASTEXITCODE" }

# npm may preserve a dependency under a package-local node_modules even when the
# same Node resolution semantics allow it to live at the Pi root. Normalize known
# path-heavy production dependencies before NSIS/artifact creation. This changes
# only physical placement, not package APIs, and is verified by an import probe.
& (Join-Path $PSScriptRoot 'normalize-runtime-layout.ps1') -Root $Destination
if ($LASTEXITCODE -ne 0) { throw "Runtime layout normalization failed with exit code $LASTEXITCODE" }

$required = @(
    'MiaoDesk.exe',
    'MiaoDeskWallpaper.exe',
    'MiaoDeskHarness.exe',
    'Assets\MiaoMiao.ico',
    'Runtime\Node\node.exe',
    'Runtime\Node\node_modules\@deepseek-ai\dsh\lib\bin.js',
    'Pi\node_modules\@earendil-works\pi-coding-agent\dist\cli.js',
    'Goz\goz.exe',
    'Goz\gozd.exe',
    'Wallpapers\MiaoCloud.mdwall\manifest.json',
    'Wallpapers\MiaoCloud.mdwall\scene.ini',
    'Wallpapers\MiaoCloud.mdwall\assets\background.jpg',
    'Wallpapers\MiaoCloud.mdwall\assets\cat.png',
    'Wallpapers\MiaoCloud.mdwall\assets\tail.png',
    'Wallpapers\MiaoCloud.mdwall\assets\blink.png',
    'Wallpapers\NeonCity.mdwall\manifest.json',
    'Wallpapers\NeonCity.mdwall\scene.ini',
    'Wallpapers\MysticMoon.mdwall\manifest.json',
    'Wallpapers\MysticMoon.mdwall\scene.ini'
)
foreach ($relative in $required) { Assert-File $Destination $relative }

# Guard against accidentally packaging Git LFS pointer files as wallpaper images.
foreach ($relative in @(
    'Wallpapers\MiaoCloud.mdwall\assets\background.jpg',
    'Wallpapers\MiaoCloud.mdwall\assets\cat.png',
    'Wallpapers\MiaoCloud.mdwall\assets\tail.png',
    'Wallpapers\MiaoCloud.mdwall\assets\blink.png'
)) {
    $path = Join-Path $Destination $relative
    if ((Get-Item $path).Length -lt 1024) { throw "Wallpaper image still looks like an LFS pointer: $relative" }
}

Set-Content -Path (Join-Path $Destination 'FULL-TEST-COMPONENTS.txt') -Encoding UTF8 -Value @(
    "Architecture: $Architecture"
    'Native files and wallpaper assets: CMake install staging'
    'MiaoDesk native shell/search/settings'
    'MiaoDeskWallpaper desktop engine'
    'MiaoDeskHarness workbench host'
    'Shared portable Node runtime'
    'DeepSeek Harness dsh production dependencies'
    'Pi Agent production dependencies (path-normalized for stock Windows)'
    'goz + gozd native file search runtime'
    'MiaoCloud / NeonCity / MysticMoon wallpaper packages'
    'Repository SDK/source/build intermediate trees are excluded'
)

Write-Host "Complete $Architecture package staging is ready: $Destination" -ForegroundColor Green
