[CmdletBinding()]
param(
    [string]$SourcePath = (Join-Path $PSScriptRoot '..\..\src\desktop\wallpaper\library\WallpaperLibrary.cpp')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $SourcePath -PathType Leaf)) {
    throw "WallpaperLibrary.cpp not found: $SourcePath"
}

$source = Get-Content -LiteralPath $SourcePath -Raw -Encoding UTF8

function Get-MethodBody([string]$name) {
    $pattern = "(?ms)std::vector<WallpaperLibraryItem> WallpaperLibrary::$name\([^)]*\) const \{(?<body>.*?)\n\}"
    $match = [regex]::Match($source, $pattern)
    if (-not $match.Success) {
        throw "WallpaperLibrary::$name was not found."
    }
    return $match.Groups['body'].Value
}

$search = Get-MethodBody 'Search'
$recent = Get-MethodBody 'RecentlyUsed'
$favorites = Get-MethodBody 'Favorites'

if ($search -notmatch 'IsLibraryUiVisible\s*\(') {
    throw 'Search() must use IsLibraryUiVisible().'
}
if ($recent -notmatch 'IsLibraryUiVisible\s*\(') {
    throw 'RecentlyUsed() must use IsLibraryUiVisible().'
}
if ($favorites -notmatch 'IsLibraryUiVisible\s*\(') {
    throw 'Favorites() must use IsLibraryUiVisible().'
}

$find = [regex]::Match($source, '(?ms)std::optional<WallpaperLibraryItem> WallpaperLibrary::Find\([^)]*\) const \{(?<body>.*?)\n\}')
if (-not $find.Success) { throw 'WallpaperLibrary::Find was not found.' }
$findBody = $find.Groups['body'].Value
$exactIndex = $findBody.IndexOf('FindIndex(id)')
$aliasIndex = $findBody.IndexOf('CanonicalBuiltinWallpaperSource')
if ($exactIndex -lt 0 -or $aliasIndex -lt 0 -or $aliasIndex -lt $exactIndex) {
    throw 'Find() must keep exact lookup before legacy alias resolution.'
}

Write-Host 'Wallpaper derived-view canonical gate source checks passed.'
