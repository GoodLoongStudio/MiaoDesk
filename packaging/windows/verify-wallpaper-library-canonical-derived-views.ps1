$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$libraryPath = Join-Path $repoRoot 'src/desktop/wallpaper/library/WallpaperLibrary.cpp'
if (-not (Test-Path -LiteralPath $libraryPath)) { throw "WallpaperLibrary.cpp not found: $libraryPath" }

$source = Get-Content -LiteralPath $libraryPath -Raw -Encoding UTF8

function Assert-Contains([string]$text, [string]$needle, [string]$message) {
    if ($text.IndexOf($needle, [System.StringComparison]::Ordinal) -lt 0) {
        throw $message
    }
}

function Assert-NotContains([string]$text, [string]$needle, [string]$message) {
    if ($text.IndexOf($needle, [System.StringComparison]::Ordinal) -ge 0) {
        throw $message
    }
}

Assert-Contains $source 'bool IsLibraryUiVisible(const WallpaperLibraryItem& item)' 'Missing shared canonical UI visibility gate.'
Assert-Contains $source 'if (!IsLibraryUiVisible(item)) continue;' 'Search() must use the shared canonical UI visibility gate.'

$recentBlock = [regex]::Match($source, 'std::vector<WallpaperLibraryItem> WallpaperLibrary::RecentlyUsed[\s\S]*?\n}\n\nstd::vector<WallpaperLibraryItem> WallpaperLibrary::Favorites')
if (-not $recentBlock.Success) { throw 'RecentlyUsed() implementation not found.' }
Assert-Contains $recentBlock.Value 'IsLibraryUiVisible(item)' 'RecentlyUsed() must filter through IsLibraryUiVisible().' 
Assert-NotContains $recentBlock.Value 'if (item.lastUsedUnixSeconds > 0) result.push_back(item);' 'RecentlyUsed() must not reintroduce an unfiltered legacy path.'

$favoritesBlock = [regex]::Match($source, 'std::vector<WallpaperLibraryItem> WallpaperLibrary::Favorites[\s\S]*?\n}\n\nconst fs::path& WallpaperLibrary::Root')
if (-not $favoritesBlock.Success) { throw 'Favorites() implementation not found.' }
Assert-Contains $favoritesBlock.Value 'IsLibraryUiVisible(item)' 'Favorites() must filter through IsLibraryUiVisible().' 
Assert-NotContains $favoritesBlock.Value 'return item.favorite;' 'Favorites() must not expose favorites without the canonical gate.'

$findBlock = [regex]::Match($source, 'std::optional<WallpaperLibraryItem> WallpaperLibrary::Find[\s\S]*?\n}\n\nstd::vector<WallpaperLibraryItem> WallpaperLibrary::Search')
if (-not $findBlock.Success) { throw 'Find() implementation not found.' }
Assert-Contains $findBlock.Value 'if (const auto index = FindIndex(id)) return items_[*index];' 'Find() must preserve exact lookup first.'
Assert-Contains $findBlock.Value 'CanonicalBuiltinWallpaperSource(id)' 'Find() must use the allowlisted canonical alias helper only after exact miss.'

$findAlias = $findBlock.Value.IndexOf('const std::wstring_view canonical = CanonicalBuiltinWallpaperSource(id)', [System.StringComparison]::Ordinal)
$findExact = $findBlock.Value.IndexOf('if (const auto index = FindIndex(id)) return items_[*index];', [System.StringComparison]::Ordinal)
if ($findAlias -ge 0 -and $findExact -ge 0 -and $findAlias -lt $findExact) {
    throw 'Find() must perform exact lookup before legacy alias resolution.'
}

$forbidden = @(
    'SaveItem(',
    'WritePrivateProfileStringW('
)
foreach ($needle in $forbidden) {
    if ($findBlock.Value.IndexOf($needle, [System.StringComparison]::Ordinal) -ge 0) {
        throw "Find() must remain read-only; found forbidden call: $needle"
    }
}

$visibilityBlock = [regex]::Match($source, 'bool IsLibraryUiVisible\(const WallpaperLibraryItem& item\)[\s\S]*?\n}\n\n} // namespace')
if (-not $visibilityBlock.Success) { throw 'IsLibraryUiVisible() implementation not found.' }
Assert-Contains $visibilityBlock.Value 'FindLegacyBuiltinWallpaper(item.id) == nullptr' 'Canonical UI gate must hide only shipped legacy built-in identities.'
Assert-NotContains $visibilityBlock.Value 'CanonicalBuiltinWallpaperSource(item.id)' 'UI visibility gate must not broaden into generic alias lookup.'

Write-Host 'Canonical derived-view verifier passed.'
