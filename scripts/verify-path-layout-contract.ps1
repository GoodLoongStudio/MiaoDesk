param([string]$RepoRoot = (Split-Path $PSScriptRoot -Parent))

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Fail([string]$Message) { throw "Path layout contract violation: $Message" }

$cmakePresets = Join-Path $RepoRoot 'CMakePresets.json'
$pathContract = Join-Path $RepoRoot 'docs\PATH_LAYOUT_CONTRACT.md'
$nativeRoot = Join-Path $RepoRoot 'src\native'
$webViewSdk = Join-Path $RepoRoot 'third_party\webview2\1.0.4129.50\build\native\include\WebView2.h'
$packagingRoots = @(
    (Join-Path $RepoRoot 'scripts'),
    (Join-Path $RepoRoot 'packaging'),
    (Join-Path $RepoRoot '.github\workflows')
)

if (-not (Test-Path $pathContract -PathType Leaf)) { Fail 'docs/PATH_LAYOUT_CONTRACT.md is missing.' }
if (-not (Test-Path $cmakePresets -PathType Leaf)) { Fail 'CMakePresets.json is missing.' }
if (-not (Test-Path $webViewSdk -PathType Leaf)) { Fail 'canonical third_party WebView2 SDK is missing.' }
foreach ($arch in @('x64','arm64')) {
    if (Test-Path (Join-Path $RepoRoot "runtime\$arch\webview2-sdk")) {
        Fail "build-only WebView2 SDK returned under runtime/$arch."
    }
}

$presets = Get-Content $cmakePresets -Raw
if ($presets -match [regex]::Escape('${sourceDir}/build') -or $presets -match [regex]::Escape('${sourceDir}\\build')) {
    Fail 'CMakePresets.json puts build output back under the source tree.'
}

$nativeFiles = @(Get-ChildItem $nativeRoot -Recurse -File -Include *.cpp,*.cc,*.cxx,*.h,*.hpp,*.inc -ErrorAction SilentlyContinue)
$cwdPatterns = @(
    'GetCurrentDirectoryW\s*\(',
    'GetCurrentDirectoryA\s*\(',
    'SetCurrentDirectoryW\s*\(',
    'SetCurrentDirectoryA\s*\(',
    'std::filesystem::current_path\s*\(',
    '\bfs::current_path\s*\('
)
foreach ($file in $nativeFiles) {
    $text = Get-Content $file.FullName -Raw -ErrorAction SilentlyContinue
    foreach ($pattern in $cwdPatterns) {
        if ($text -match $pattern) {
            Fail "production native source depends on current working directory: $($file.FullName.Substring($RepoRoot.Length + 1))"
        }
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

$appMain = Join-Path $nativeRoot 'src\app\main.cpp'
$harnessBootstrap = Join-Path $nativeRoot 'src\harness\HarnessBundledRuntimeBootstrap.cpp'
foreach ($file in @($appMain,$harnessBootstrap)) {
    if (-not (Test-Path $file -PathType Leaf)) { Fail "runtime bootstrap source is missing: $file" }
    if ((Get-Content $file -Raw) -notmatch 'GetModuleFileNameW\s*\(') {
        Fail "runtime discovery is no longer executable-relative: $file"
    }
}

Write-Host 'Path layout contract OK.' -ForegroundColor Green
