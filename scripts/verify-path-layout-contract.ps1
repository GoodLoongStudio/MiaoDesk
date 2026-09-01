param([string]$RepoRoot = (Split-Path $PSScriptRoot -Parent))

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Fail([string]$Message) { throw "Path layout contract violation: $Message" }

$cmakePresets = Join-Path $RepoRoot 'CMakePresets.json'
$pathContract = Join-Path $RepoRoot 'docs\PATH_LAYOUT_CONTRACT.md'
$sourceRoot = Join-Path $RepoRoot 'src'
$sourceCMake = Join-Path $sourceRoot 'CMakeLists.txt'
$appPathsHeader = Join-Path $sourceRoot 'include\miaodesk\AppPaths.h'
$productConfig = Join-Path $RepoRoot 'config\product.ini'
$agentRuntimeScript = Join-Path $RepoRoot 'packaging\windows\build-agent-runtime.ps1'
$webViewRoot = Join-Path $RepoRoot 'third_party\webview2'
$packagingRoots = @(
    (Join-Path $RepoRoot 'scripts'),
    (Join-Path $RepoRoot 'packaging'),
    (Join-Path $RepoRoot '.github\workflows')
)

if (-not (Test-Path $pathContract -PathType Leaf)) { Fail 'docs/PATH_LAYOUT_CONTRACT.md is missing.' }
if (-not (Test-Path $cmakePresets -PathType Leaf)) { Fail 'CMakePresets.json is missing.' }
if (-not (Test-Path $sourceRoot -PathType Container)) { Fail 'src/ is missing.' }
if (-not (Test-Path $sourceCMake -PathType Leaf)) { Fail 'src/CMakeLists.txt is missing.' }
if (-not (Test-Path $appPathsHeader -PathType Leaf)) { Fail 'shared AppPaths.h is missing.' }
if (-not (Test-Path $productConfig -PathType Leaf)) { Fail 'config/product.ini is missing.' }
if (-not (Test-Path $agentRuntimeScript -PathType Leaf)) { Fail 'Agent Runtime staging script is missing.' }
if (Test-Path (Join-Path $sourceRoot 'native')) { Fail 'obsolete src/native container returned.' }
foreach ($relative in @('app','ai','desktop','harness','search','ui','include\miaodesk')) {
    if (-not (Test-Path (Join-Path $sourceRoot $relative) -PathType Container)) {
        Fail "source domain is missing: src/$($relative -replace '\\','/')"
    }
}
if (@(Get-ChildItem $sourceRoot -File -Include *.cpp,*.cc,*.cxx -ErrorAction SilentlyContinue).Count -gt 0) {
    Fail 'implementation files must live in a source domain, not directly under src/.'
}

# Each implementation file has one CMake source-list owner. Shared code belongs
# in a library target instead of being compiled separately into multiple EXEs.
$sourceOwners = @{}
foreach ($line in Get-Content $sourceCMake) {
    if ($line -notmatch '^\s+([A-Za-z0-9_./-]+\.cpp)\s*$') { continue }
    $relative = $Matches[1]
    if ($sourceOwners.ContainsKey($relative)) {
        Fail "implementation has multiple CMake owners: src/$relative"
    }
    $sourceOwners[$relative] = $true
    if (-not (Test-Path (Join-Path $sourceRoot $relative) -PathType Leaf)) {
        Fail "CMake source is missing: src/$relative"
    }
}

foreach ($relative in @(
    'LICENSE.txt',
    'NOTICE.txt',
    'include\WebView2.h',
    'include\WebView2EnvironmentOptions.h',
    'lib\x64\WebView2LoaderStatic.lib',
    'lib\arm64\WebView2LoaderStatic.lib'
)) {
    if (-not (Test-Path (Join-Path $webViewRoot $relative) -PathType Leaf)) {
        Fail "minimal WebView2 SDK file is missing: third_party/webview2/$($relative -replace '\\','/')"
    }
}
foreach ($obsolete in @('build','include-winrt','1.0.4129.50','lib\x86','manifest.json')) {
    if (Test-Path (Join-Path $webViewRoot $obsolete)) {
        Fail "obsolete WebView2 SDK layout returned: third_party/webview2/$($obsolete -replace '\\','/')"
    }
}
foreach ($arch in @('x64','arm64')) {
    if (Test-Path (Join-Path $RepoRoot "runtime\$arch\webview2-sdk")) {
        Fail "build-only WebView2 SDK returned under runtime/$arch."
    }
}

$agentRuntimeText = Get-Content $agentRuntimeScript -Raw
foreach ($requiredPattern in @(
    'Join-Path \$Root ''Agent''',
    'Join-Path \$agentModules ''pi''',
    '@\(''pi'',''pi\.cmd'',''pi\.ps1''\)',
    '\.Name -like ''\*\.d\.ts''',
    '\.Name -like ''\*\.js\.map'''
)) {
    if ($agentRuntimeText -notmatch $requiredPattern) {
        Fail "Agent Runtime short-path normalization is missing: pattern=$requiredPattern"
    }
}
if ($agentRuntimeText -match 'Join-Path \$Root ''Runtime\\Agent''') {
    Fail 'Agent dependency graph returned to the over-budget Runtime/Agent path.'
}

$presets = Get-Content $cmakePresets -Raw
if ($presets -match [regex]::Escape('${sourceDir}/build') -or $presets -match [regex]::Escape('${sourceDir}\\build')) {
    Fail 'CMakePresets.json puts build output back under the source tree.'
}

$sourceFiles = @(Get-ChildItem $sourceRoot -Recurse -File -Include *.cpp,*.cc,*.cxx,*.h,*.hpp,*.inc -ErrorAction SilentlyContinue)
$cwdPatterns = @(
    'GetCurrentDirectoryW\s*\(',
    'GetCurrentDirectoryA\s*\(',
    'SetCurrentDirectoryW\s*\(',
    'SetCurrentDirectoryA\s*\(',
    'std::filesystem::current_path\s*\(',
    '\bfs::current_path\s*\('
)
foreach ($file in $sourceFiles) {
    $text = Get-Content $file.FullName -Raw -ErrorAction SilentlyContinue
    foreach ($pattern in $cwdPatterns) {
        if ($text -match $pattern) {
            Fail "production native source depends on current working directory: $($file.FullName.Substring($RepoRoot.Length + 1))"
        }
    }
}

$localStateAllowlist = @(
    'src\include\miaodesk\AppPaths.h',
    'src\ai\agent\L3PersistenceSelfTest.cpp',
    'src\desktop\wallpaper\runtime\WallpaperEntry.cpp'
)
foreach ($file in $sourceFiles) {
    $relative = $file.FullName.Substring($RepoRoot.Length + 1)
    if ($localStateAllowlist -contains $relative) { continue }
    $text = Get-Content $file.FullName -Raw -ErrorAction SilentlyContinue
    if ($text -match 'GetEnvironmentVariableW\s*\(\s*L"LOCALAPPDATA"' -or
        $text -match 'FOLDERID_LocalAppData') {
        Fail "native source bypasses shared AppPaths: $relative"
    }
}

$forbiddenPackagingPatterns = @(
    'set\s+"?DEST=C:\\MD(?:\\|"|$)',
    'InstallDir\s+"?\$LOCALAPPDATA\\Programs\\MiaoDesk',
    '(?i)reg(?:\.exe)?\s+add[^\r\n]*LongPathsEnabled',
    '(?i)Set-ItemProperty[^\r\n]*LongPathsEnabled',
    '(?i)New-ItemProperty[^\r\n]*LongPathsEnabled',
    '(?i)HKLM\\SYSTEM\\CurrentControlSet\\Control\\FileSystem[^\r\n]*(LongPathsEnabled|LongPaths)'
)
foreach ($root in $packagingRoots) {
    if (-not (Test-Path $root -PathType Container)) { continue }
    foreach ($file in @(Get-ChildItem $root -Recurse -File -Include *.ps1,*.cmd,*.bat,*.nsi,*.yml,*.yaml,*.cmake -ErrorAction SilentlyContinue)) {
        if ($file.Name -eq 'verify-path-layout-contract.ps1') { continue }
        $text = Get-Content $file.FullName -Raw -ErrorAction SilentlyContinue
        foreach ($pattern in $forbiddenPackagingPatterns) {
            if ($text -match $pattern) {
                Fail "forbidden installer/packaging path policy found in $($file.FullName.Substring($RepoRoot.Length + 1)): pattern=$pattern"
            }
        }
    }
}

$appMain = Join-Path $sourceRoot 'app\main.cpp'
$harnessBootstrap = Join-Path $sourceRoot 'harness\HarnessBundledRuntimeBootstrap.cpp'
foreach ($file in @($appMain,$harnessBootstrap)) {
    if (-not (Test-Path $file -PathType Leaf)) { Fail "runtime bootstrap source is missing: $file" }
    if ((Get-Content $file -Raw) -notmatch 'GetModuleFileNameW\s*\(') {
        Fail "runtime discovery is no longer executable-relative: $file"
    }
}

Write-Host 'Path layout contract OK.' -ForegroundColor Green
