$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$libraryPath = Join-Path $repoRoot 'src/desktop/wallpaper/library/WallpaperLibrary.cpp'
if (-not (Test-Path -LiteralPath $libraryPath)) { throw "WallpaperLibrary.cpp not found: $libraryPath" }

$source = Get-Content -LiteralPath $libraryPath -Raw -Encoding UTF8
# 仓库里统一 LF(.gitattributes 的 * text=auto),但 Windows runner 检出时是 CRLF。
# 下面这些正则用 \n 定位"空行"与"函数结尾",CRLF 下空行是 \r\n\r\n,\n\n 永远
# 匹配不上 —— 于是闸门在 Windows 上必失败,报的是 RecentlyUsed() implementation not
# found 这种和真实问题无关的消息。2026-09-22 c55ef67 第一次在 push 上跑就踩到:
# 本机(LF)全绿、Windows CI 全红,差别只在行尾。在读入处归一化,比改十几处正则可靠。
$source = $source -replace "`r`n", "`n"

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
