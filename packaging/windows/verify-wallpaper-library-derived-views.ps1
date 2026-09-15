[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$SourcePath
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $SourcePath -PathType Leaf)) {
    throw "WallpaperLibrary source file does not exist: $SourcePath"
}

$text = Get-Content -LiteralPath $SourcePath -Raw -Encoding UTF8

function Get-MethodBody([string]$name) {
    $pattern = '(?s)std::vector<WallpaperLibraryItem>\\s+WallpaperLibrary::' + [regex]::Escape($name) + '\\([^)]*\\)\\s+const\\s*\\{(.*?)\\n\\}'
    $match = [regex]::Match($text, $pattern)
    if (-not $match.Success) {
        throw "Unable to locate WallpaperLibrary::$name implementation."
    }
    return $match.Groups[1].Value
}

foreach ($name in @('Search', 'RecentlyUsed', 'Favorites')) {
    $body = Get-MethodBody $name
    if ($body -notmatch 'IsLibraryUiVisible\\s*\\(') {
        throw "WallpaperLibrary::$name must apply IsLibraryUiVisible() to keep shipped legacy scene-* rows out of UI-derived views."
    }
    if ($name -ne 'Search' -and $body -match 'FindLegacyBuiltinWallpaper\\s*\\(') {
        throw "WallpaperLibrary::$name must reuse the shared IsLibraryUiVisible() gate instead of duplicating a broader legacy filter."
    }
}

Write-Host 'WallpaperLibrary canonical UI gate verification passed for Search, RecentlyUsed, and Favorites.'
