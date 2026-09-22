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
# 单引号 PowerShell 字符串是字面量:写进去两个反斜杠,收到就是两个反斜杠。
# .NET 正则把双反斜杠读成"一个字面反斜杠 + 后面那个字符本身的含义",所以:
#   - 单引号里写  [\s\S]  实际想表达任意空白,正则读到的是字符类 [\sS]
#     —— 也就是"反斜杠、s、S 三者之一",于是永远匹配不上函数体;
#   - 单引号里写  \n      正则读到的是字面量"反斜杠加 n",不是换行;
#   - 单引号里写  \(  \)  正则读到的是"一个字面反斜杠 + 一个捕获组的开始/结束",
#     于是这条 pattern 在找一个*签名里带反斜杠*的函数 —— 源码里当然没有。
# 三处同犯不是笔误,而是把 PowerShell 的转义符当成了反斜杠。
# **PowerShell 的转义符是反引号(`),单引号字符串连反引号都不解释。**
# 要匹配字面圆括号,单引号里只能写一个反斜杠。
# 2026-09-22:c55ef67 第一次把本文件接进工作流,第一次跑就被上一步的 CRLF
# 问题挡住没走到这里;CRLF 修好之后这一层错误才暴露出来。两层叠在一起,
# 所以第一轮看到的"根因是 CRLF"只对了一半。

function Assert-Contains([string]$text, [string]$needle, [string]$message) {
    if ($text.IndexOf($needle, [System.StringComparison]::Ordinal) -lt 0) { throw $message }
}

function Assert-NotContains([string]$text, [string]$needle, [string]$message) {
    if ($text.IndexOf($needle, [System.StringComparison]::Ordinal) -ge 0) { throw $message }
}

function Get-FunctionBlock([string]$name, [string]$nextName) {
    $pattern = [regex]::Escape($name) + '[\s\S]*?\n}\n\n' + [regex]::Escape($nextName)
    $match = [regex]::Match($source, $pattern)
    if (-not $match.Success) { throw "Function block not found: $name" }
    # 返回 Match 而不是 $match.Value。调用点写的是 $block.Value —— 那是 Match 的成员。
    # 若这里返回 string,$block.Value 在 PowerShell 7 上**不报错**,静默返回空串,
    # 于是每条断言都以"内容里没有这句话"失败,报的是 Search()/RecentlyUsed() 的错,
    # 而真正错的是这个门自己。PS 5 上同样不报错。
    # 实测:("hello").Value -eq ''。所以这里必须把 Match 交出去,让 .Value 有意义。
    return $match
}

$gate = [regex]::Match($source, 'bool IsLibraryUiVisible\(const WallpaperLibraryItem& item\)[\s\S]*?\n}\n\n} // namespace')
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
