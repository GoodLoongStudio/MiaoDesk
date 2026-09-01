param(
    [string]$RepoRoot = (Split-Path $PSScriptRoot -Parent)
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Fail([string]$Message) { throw "Path layout contract violation: $Message" }

$cmakePresets = Join-Path $RepoRoot 'CMakePresets.json'
$pathContract = Join-Path $RepoRoot 'docs\PATH_LAYOUT_CONTRACT.md'
$nativeRoot = Join-Path $RepoRoot 'src\native'
$packagingRoots = @(
    (Join-Path $RepoRoot 'scripts'),
    (Join-Path $RepoRoot 'packaging'),
    (Join-Path $RepoRoot '.github\workflows')
)

if (-not (Test-Path $pathContract -PathType Leaf)) { Fail 'docs/PATH_LAYOUT_CONTRACT.md is missing.' }
if (-not (Test-Path $cmakePresets -PathType Leaf)) { Fail 'CMakePresets.json is missing.' }

$presets = Get-Content $cmakePresets -Raw
if ($presets -match [regex]::Escape('${sourceDir}/build') -or $presets -match [regex]::Escape('${sourceDir}\\build')) {
    Fail 'CMakePresets.json puts build output back under the source tree.'
}

# Production native code must not locate shipped files from the process CWD.
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

# Packaging may mention long-path policy in comments/readmes, but must never
# enable/mutate that OS policy as part of installation or package preparation.
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

# Runtime discovery must keep at least one executable-directory anchor in both
# the application and Harness bootstrap. This is deliberately a structural
# contract rather than a hard-coded helper name.
$appMain = Join-Path $nativeRoot 'src\app\main.cpp'
$harnessBootstrapCandidates = @(
    (Join-Path $nativeRoot 'src\harness\HarnessBundledRuntimeBootstrap.cpp'),
    (Join-Path $nativeRoot 'src\harness\runtime\HarnessBundledRuntimeBootstrap.cpp')
)
if (-not (Test-Path $appMain -PathType Leaf)) { Fail 'native app main.cpp is missing.' }
$appText = Get-Content $appMain -Raw
if ($appText -notmatch 'GetModuleFileNameW\s*\(') {
    Fail 'MiaoDesk app no longer anchors shipped runtime paths to the executable/module directory.'
}
$bootstrap = $harnessBootstrapCandidates | Where-Object { Test-Path $_ -PathType Leaf } | Select-Object -First 1
if (-not $bootstrap) { Fail 'HarnessBundledRuntimeBootstrap.cpp is missing.' }
$bootstrapText = Get-Content $bootstrap -Raw
if ($bootstrapText -notmatch 'GetModuleFileNameW\s*\(') {
    Fail 'Harness bootstrap no longer anchors bundled runtime paths to its executable/module directory.'
}

Write-Host 'Path layout contract OK: external build roots, executable-relative runtime discovery, and stock-Windows packaging policy remain enforced.' -ForegroundColor Green
