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
# 仓库里统一 LF(.gitattributes 的 * text=auto),但 Windows runner 检出时是 CRLF。
# 下面这些正则用 \n 定位"空行"与"函数结尾",CRLF 下空行是 \r\n\r\n,\n\n 永远
# 匹配不上 —— 于是闸门在 Windows 上必失败,报的是 RecentlyUsed() implementation not
# found 这种和真实问题无关的消息。2026-09-22 c55ef67 第一次在 push 上跑就踩到:
# 本机(LF)全绿、Windows CI 全红,差别只在行尾。在读入处归一化,比改十几处正则可靠。
$source = $source -replace "`r`n", "`n"

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
