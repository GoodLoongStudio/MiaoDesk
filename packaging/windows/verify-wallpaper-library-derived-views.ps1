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
    $pattern = '(?s)std::vector<WallpaperLibraryItem>\s+WallpaperLibrary::' + [regex]::Escape($name) + '\([^)]*\)\s+const\s*\{(.*?)\n\}'
    $match = [regex]::Match($text, $pattern)
    if (-not $match.Success) {
        throw "Unable to locate WallpaperLibrary::$name implementation."
    }
    return $match.Groups[1].Value
}

foreach ($name in @('Search', 'RecentlyUsed', 'Favorites')) {
    $body = Get-MethodBody $name
    if ($body -notmatch 'IsLibraryUiVisible\s*\(') {
        throw "WallpaperLibrary::$name must apply IsLibraryUiVisible() to keep shipped legacy scene-* rows out of UI-derived views."
    }
    if ($name -ne 'Search' -and $body -match 'FindLegacyBuiltinWallpaper\s*\(') {
        throw "WallpaperLibrary::$name must reuse the shared IsLibraryUiVisible() gate instead of duplicating a broader legacy filter."
    }
}

$findPattern = '(?s)std::optional<WallpaperLibraryItem>\s+WallpaperLibrary::Find\([^)]*\)\s+const\s*\{(.*?)\n\}'
$findMatch = [regex]::Match($text, $findPattern)
if (-not $findMatch.Success) {
    throw 'Unable to locate WallpaperLibrary::Find implementation.'
}
$findBody = $findMatch.Groups[1].Value

$exactIndex = $findBody.IndexOf('FindIndex(id)')
$aliasIndex = $findBody.IndexOf('CanonicalBuiltinWallpaperSource(id)')
if ($exactIndex -lt 0 -or $aliasIndex -lt 0 -or $exactIndex -gt $aliasIndex) {
    throw 'WallpaperLibrary::Find must perform exact lookup before allowlisted legacy alias resolution.'
}
if ($findBody -match 'WritePrivateProfileStringW' -or $findBody -match 'SaveItem\s*\(') {
    throw 'WallpaperLibrary::Find must remain read-only and must not rewrite persisted library or monitor assignments.'
}

$uiGate = [regex]::Match($text, '(?s)bool\s+IsLibraryUiVisible\([^)]*\)\s*\{(.*?)\n\}')
if (-not $uiGate.Success -or $uiGate.Groups[1].Value -notmatch 'FindLegacyBuiltinWallpaper\s*\(') {
    throw 'IsLibraryUiVisible must remain the single shipped legacy scene-* visibility predicate.'
}

Write-Host 'WallpaperLibrary canonical UI gate verification passed for Search, RecentlyUsed, Favorites, and exact-first Find alias resolution.'
