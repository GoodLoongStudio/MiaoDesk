param(
    [string]$PackPath = (Join-Path (Split-Path $PSScriptRoot -Parent) 'assets\wallpapers\BuiltinWallpapers.mdpack'),
    [string]$MetadataRoot = (Join-Path (Split-Path $PSScriptRoot -Parent) 'assets\wallpapers'),
    [Parameter(Mandatory=$true)][string]$Destination
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

# MiaoCloud already ships its production assets. This compact binary pack
# carries the two other built-in mdwall themes. Their effect overlays are
# aggressively optimized because they contain glow/rain/fog rather than detail.
$themes = @('NeonCity.mdwall','MysticMoon.mdwall')
$requiredAssets = @{
    'NeonCity.mdwall' = @('background.jpg','city_glow.png','haze.png','rain_1.png','rain_2.png')
    'MysticMoon.mdwall' = @('background.jpg','moon_glow.png','water_glow.png','fog.png','fireflies.png')
}

$stage = Join-Path $env:TEMP ('MiaoDesk-Wallpapers-' + [guid]::NewGuid().ToString('N'))
$zipPath = Join-Path $stage 'BuiltinWallpapers.zip'
$extract = Join-Path $stage 'extract'
try {
    New-Item -ItemType Directory -Force -Path $stage,$extract,$Destination | Out-Null

    if (Test-Path $PackPath -PathType Leaf) {
        Copy-Item $PackPath $zipPath -Force
    }
    else {
        $partRoot = Join-Path $MetadataRoot '.pack'
        $parts = @(Get-ChildItem $partRoot -Filter 'BuiltinWallpapers.mdpack.part*' -File -ErrorAction SilentlyContinue | Sort-Object Name)
        if ($parts.Count -eq 0) { throw "Builtin wallpaper pack is missing: $PackPath" }
        $out = [IO.File]::Open($zipPath, [IO.FileMode]::Create, [IO.FileAccess]::Write, [IO.FileShare]::None)
        try {
            foreach ($part in $parts) {
                $bytes = [IO.File]::ReadAllBytes($part.FullName)
                $out.Write($bytes, 0, $bytes.Length)
            }
        } finally { $out.Dispose() }
    }

    if ((Get-Item $zipPath).Length -lt 300000) { throw "Builtin wallpaper pack is unexpectedly small: $zipPath" }

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = [IO.Compression.ZipFile]::OpenRead($zipPath)
    try {
        foreach ($entry in $archive.Entries) {
            $name = $entry.FullName.Replace('\','/')
            if ([string]::IsNullOrWhiteSpace($name)) { continue }
            if ($name.StartsWith('/') -or $name.Contains('../') -or $name.Contains('..\')) {
                throw "Unsafe builtin wallpaper pack entry: $name"
            }
            if ($name -notmatch '^(NeonCity|MysticMoon)\.mdwall/assets/[A-Za-z0-9_.-]+$') {
                throw "Unexpected builtin wallpaper pack entry: $name"
            }
        }
    } finally { $archive.Dispose() }

    Expand-Archive -Path $zipPath -DestinationPath $extract -Force

    foreach ($theme in $themes) {
        $sourceTheme = Join-Path $MetadataRoot $theme
        $destTheme = Join-Path $Destination $theme
        New-Item -ItemType Directory -Force -Path $destTheme | Out-Null

        foreach ($meta in @('manifest.json','scene.ini','README.md')) {
            $sourceMeta = Join-Path $sourceTheme $meta
            if (Test-Path $sourceMeta -PathType Leaf) {
                Copy-Item $sourceMeta (Join-Path $destTheme $meta) -Force
            }
        }
        if (-not (Test-Path (Join-Path $destTheme 'manifest.json') -PathType Leaf)) { throw "Wallpaper manifest missing: $theme" }
        if (-not (Test-Path (Join-Path $destTheme 'scene.ini') -PathType Leaf)) { throw "Wallpaper scene config missing: $theme" }

        $sourceAssets = Join-Path (Join-Path $extract $theme) 'assets'
        $destAssets = Join-Path $destTheme 'assets'
        Remove-Item $destAssets -Recurse -Force -ErrorAction SilentlyContinue
        New-Item -ItemType Directory -Force -Path $destAssets | Out-Null
        Copy-Item (Join-Path $sourceAssets '*') $destAssets -Recurse -Force

        foreach ($assetName in $requiredAssets[$theme]) {
            $asset = Join-Path $destAssets $assetName
            if (-not (Test-Path $asset -PathType Leaf)) { throw "Builtin wallpaper asset missing: $theme/assets/$assetName" }
            if ((Get-Item $asset).Length -lt 1024) { throw "Builtin wallpaper asset is invalid: $theme/assets/$assetName" }
        }
    }

    Write-Host "Builtin wallpapers materialized: $Destination" -ForegroundColor Green
} finally {
    Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue
}
