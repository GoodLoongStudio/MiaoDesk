$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$libraryPath = Join-Path $repoRoot 'src/desktop/wallpaper/library/WallpaperLibrary.cpp'
if (-not (Test-Path -LiteralPath $libraryPath)) { throw "WallpaperLibrary.cpp not found: $libraryPath" }

$source = Get-Content -LiteralPath $libraryPath -Raw -Encoding UTF8

function Assert-Contains([string]$text, [string]$needle, [string]$message) {
    if ($text.IndexOf($needle, [System.StringComparison]::Ordinal) -lt 0) { throw $message }
}

function Assert-NotContains([string]$text, [string]$needle, [string]$message) {
    if ($text.IndexOf($needle, [System.StringComparison]::Ordinal) -ge 0) { throw $message }
}

function Get-FunctionBlock([string]$name, [string]$nextName) {
    $pattern = [regex]::Escape($name) + '[\\s\\S]*?\\n}\\n\\n' + [regex]::Escape($nextName)
    $match = [regex]::Match($source, $pattern)
    if (-not $match.Success) { throw "Function block not found: $name" }
    return $match.Value
}

$gate = [regex]::Match($source, 'bool IsLibraryUiVisible\\(const WallpaperLibraryItem& item\\)[\\s\\S]*?\\n}\\n\\n} // namespace')
if (-not $gate.Success) { throw 'Missing IsLibraryUiVisible() gate.' }
Assert-Contains $gate.Value 'FindLegacyBuiltinWallpaper(item.id) == nullptr' 'Visibility gate must hide only shipped legacy built-ins.'
Assert-NotContains $gate.Value 'CanonicalBuiltinWallpaperSource(item.id)' 'Visibility gate must not become a generic alias filter.'

$search = Get-FunctionBlock 'std::vector<WallpaperLibraryItem> WallpaperLibrary::Search' 'std::vector<WallpaperLibraryItem> WallpaperLibrary::RecentlyUsed'
Assert-Contains $search.Value 'if (!IsLibraryUiVisible(item)) continue;' 'Search() must use shared canonical gate.'

$recent = Get-FunctionBlock 'std::vector<WallpaperLibraryItem> WallpaperLibrary::RecentlyUsed' 'std::vector<WallpaperLibraryItem> WallpaperLibrary::Favorites'
Assert-Contains $recent.Value 'if (!IsLibraryUiVisible(item)) continue;' 'RecentlyUsed() must use shared canonical gate.'
Assert-NotContains $recent.Value 'if (item.lastUsedUnixSeconds > 0) result.push_back(item);' 'RecentlyUsed() must not retain unfiltered push-back.'

$favorites = Get-FunctionBlock 'std::vector<WallpaperLibraryItem> WallpaperLibrary::Favorites' 'const fs::path& WallpaperLibrary::Root'
Assert-Contains $favorites.Value 'if (!IsLibraryUiVisible(item)) continue;' 'Favorites() must use shared canonical gate.'
Assert-Contains $favorites.Value 'if (item.favorite) result.push_back(item);' 'Favorites() must preserve favorite semantics after filtering.'
Assert-NotContains $favorites.Value 'return item.favorite;' 'Favorites() must not expose raw predicate-only filtering.'

$find = Get-FunctionBlock 'std::optional<WallpaperLibraryItem> WallpaperLibrary::Find' 'std::vector<WallpaperLibraryItem> WallpaperLibrary::Search'
$exact = $find.Value.IndexOf('if (const auto index = FindIndex(id)) return items_[*index];', [System.StringComparison]::Ordinal)
$alias = $find.Value.IndexOf('CanonicalBuiltinWallpaperSource(id)', [System.StringComparison]::Ordinal)
if ($exact -lt 0 -or $alias -lt 0 -or $alias -lt $exact) { throw 'Find() must remain exact-first, then allowlisted alias resolution.' }
Assert-NotContains $find.Value 'SaveItem(' 'Find() must remain read-only.'
Assert-NotContains $find.Value 'WritePrivateProfileStringW(' 'Find() must not rewrite persisted assignments.'

Write-Host 'Wallpaper derived-view runtime verifier passed.'
