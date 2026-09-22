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
# 仓库里统一 LF(.gitattributes 的 * text=auto),但 Windows runner 检出时是 CRLF。
# 下面这些正则用 \n 定位"空行"与"函数结尾",CRLF 下空行是 \r\n\r\n,\n\n 永远
# 匹配不上 —— 于是闸门在 Windows 上必失败,报的是 RecentlyUsed() implementation not
# found 这种和真实问题无关的消息。2026-09-22 c55ef67 第一次在 push 上跑就踩到:
# 本机(LF)全绿、Windows CI 全红,差别只在行尾。在读入处归一化,比改十几处正则可靠。
$source = $source -replace "`r`n", "`n"

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
