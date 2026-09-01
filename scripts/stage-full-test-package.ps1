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

# Materialize the pinned offline base runtime first. x64 then replaces the V2
# per-agent dependency trees with one freshly resolved production workspace.
if ($Architecture -eq 'x64' -and
    (Test-Path (Join-Path $RepoRoot 'runtime\x64\runtime-manifest.json') -PathType Leaf) -and
    (Test-Path (Join-Path $RepoRoot 'runtime\x64\.complete') -PathType Leaf)) {
    Write-Host 'Using vendored x64 RuntimeBundle as the pinned base runtime.' -ForegroundColor Cyan
    & (Join-Path $PSScriptRoot 'prepare-third-party-runtime-x64.ps1') -DeployDir $Destination -SkipGozServiceInstall
} else {
    & (Join-Path $PSScriptRoot 'prepare-store-runtime.ps1') -DeployDir $Destination -Architecture $Architecture
}
if ($LASTEXITCODE -ne 0) { throw "Runtime materialization failed with exit code $LASTEXITCODE" }

if ($Architecture -eq 'x64') {
    # Runtime V3: DSH and Pi are installed together, producing exactly one npm
    # dependency graph and one package-lock. The only V2 paths left afterward
    # are two tiny executable compatibility entry shims, never package trees.
    & (Join-Path $PSScriptRoot 'build-unified-agent-runtime.ps1') -Root $Destination -Architecture $Architecture
    if ($LASTEXITCODE -ne 0) { throw "Unified Agent runtime build failed with exit code $LASTEXITCODE" }
} else {
    & (Join-Path $PSScriptRoot 'normalize-runtime-layout.ps1') -Root $Destination
    if ($LASTEXITCODE -ne 0) { throw "Runtime layout normalization failed with exit code $LASTEXITCODE" }
}

$required = @(
    'MiaoDesk.exe',
    'MiaoDeskWallpaper.exe',
    'MiaoDeskHarness.exe',
    'Assets\MiaoMiao.ico',
    'Runtime\Node\node.exe',
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
if ($Architecture -eq 'x64') {
    $required += @(
        'Runtime\Agent\package.json',
        'Runtime\Agent\package-lock.json',
        'Runtime\Agent\runtime-manifest.json',
        'Runtime\Agent\node_modules\@deepseek-ai\dsh\lib\bin.js',
        'Runtime\Agent\node_modules\@earendil-works\pi-coding-agent\dist\cli.js',
        'Runtime\Node\node_modules\@deepseek-ai\dsh\lib\bin.js',
        'Pi\node_modules\@earendil-works\pi-coding-agent\dist\cli.js'
    )
} else {
    $required += @(
        'Runtime\Node\node_modules\@deepseek-ai\dsh\lib\bin.js',
        'Pi\node_modules\@earendil-works\pi-coding-agent\dist\cli.js'
    )
}
foreach ($relative in $required) { Assert-File $Destination $relative }

if ($Architecture -eq 'x64') {
    if (Test-Path (Join-Path $Destination 'node_modules')) {
        throw 'Runtime V2 root node_modules tree leaked into x64 V3 package.'
    }
    $legacyPiFiles = @(Get-ChildItem (Join-Path $Destination 'Pi') -File -Recurse -Force -ErrorAction SilentlyContinue)
    $legacyDshFiles = @(Get-ChildItem (Join-Path $Destination 'Runtime\Node\node_modules') -File -Recurse -Force -ErrorAction SilentlyContinue)
    if ($legacyPiFiles.Count -ne 1) { throw "Pi compatibility area must contain exactly one shim file, found $($legacyPiFiles.Count)." }
    if ($legacyDshFiles.Count -ne 1) { throw "DSH compatibility area must contain exactly one shim file, found $($legacyDshFiles.Count)." }
}

foreach ($relative in @(
    'Wallpapers\MiaoCloud.mdwall\assets\background.jpg',
    'Wallpapers\MiaoCloud.mdwall\assets\cat.png',
    'Wallpapers\MiaoCloud.mdwall\assets\tail.png',
    'Wallpapers\MiaoCloud.mdwall\assets\blink.png'
)) {
    $path = Join-Path $Destination $relative
    if ((Get-Item $path).Length -lt 1024) { throw "Wallpaper image still looks like an LFS pointer: $relative" }
}

$agentDescription = if ($Architecture -eq 'x64') {
    'Unified Runtime V3 Agent workspace: one DSH + Pi package-lock and one node_modules tree'
} else {
    'Legacy ARM64 Agent runtime pending Runtime V3 parity migration'
}
Set-Content -Path (Join-Path $Destination 'FULL-TEST-COMPONENTS.txt') -Encoding UTF8 -Value @(
    "Architecture: $Architecture"
    'Native files and wallpaper assets: CMake install staging'
    'MiaoDesk native shell/search/settings'
    'MiaoDeskWallpaper desktop engine'
    'MiaoDeskHarness workbench host'
    'Shared portable Node runtime'
    $agentDescription
    'goz + gozd native file search runtime'
    'MiaoCloud / NeonCity / MysticMoon wallpaper packages'
    'Repository SDK/source/build intermediate trees are excluded'
)

Write-Host "Complete $Architecture package staging is ready: $Destination" -ForegroundColor Green
