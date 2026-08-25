param()

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$sourceRoot = Join-Path $root 'src/native/src'
$cmakePath = Join-Path $root 'src/native/CMakeLists.txt'
$layoutDoc = Join-Path $root 'docs/NATIVE_SOURCE_LAYOUT.md'

foreach ($path in @($sourceRoot, $cmakePath, $layoutDoc)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Missing native source layout input: $path" }
}

$requiredDirectories = @(
    'src/native/src/app',
    'src/native/src/ai/pi',
    'src/native/src/ai/tools',
    'src/native/src/ai/agent',
    'src/native/src/search',
    'src/native/src/harness',
    'src/native/src/desktop/control',
    'src/native/src/desktop/shell',
    'src/native/src/desktop/wallpaper',
    'src/native/src/desktop/wallpaper/runtime',
    'src/native/src/desktop/wallpaper/legacy',
    'src/native/src/desktop/wallpaper/library',
    'src/native/src/desktop/wallpaper/monitor',
    'src/native/src/desktop/wallpaper/render',
    'src/native/src/desktop/wallpaper/web',
    'src/native/src/desktop/widgets',
    'src/native/src/desktop/automation',
    'src/native/src/desktop/performance',
    'src/native/src/ui/search',
    'src/native/src/ui/settings',
    'src/native/src/ui/ai',
    'src/native/src/ui/wallpaper',
    'src/native/src/ui/widgets',
    'src/native/src/ui/automation',
    'src/native/src/ui/performance'
)
foreach ($relative in $requiredDirectories) {
    $path = Join-Path $root $relative
    if (-not (Test-Path -LiteralPath $path -PathType Container)) {
        throw "Native source module directory missing: $relative"
    }
}

$flatCpp = @(Get-ChildItem -LiteralPath $sourceRoot -File -Filter '*.cpp')
if ($flatCpp.Count -gt 0) {
    $names = ($flatCpp | ForEach-Object Name) -join ', '
    throw "Native implementation files must not be flattened at src/native/src: $names"
}

$cmake = Get-Content -LiteralPath $cmakePath -Raw
foreach ($marker in @(
    'src/app/main.cpp',
    'src/ai/pi/PiRuntime.cpp',
    'src/desktop/control/DesktopControlService.cpp',
    'src/desktop/shell/DesktopShellHost.cpp',
    'src/desktop/widgets/WidgetService.cpp',
    'src/desktop/automation/AutomationService.cpp',
    'src/desktop/performance/PerformanceService.cpp',
    'src/ui/wallpaper/WallpaperLibraryWindowProduction.cpp',
    'src/harness/HarnessHost.cpp',
    'source_group(TREE')) {
    if (-not $cmake.Contains($marker)) { throw "CMake source ownership marker missing: $marker" }
}
if ([regex]::IsMatch($cmake, '(?m)^\s+src/[A-Za-z0-9_]+\.cpp\s*$')) {
    throw 'CMake must not reintroduce flat src/*.cpp implementation paths.'
}

$doc = Get-Content -LiteralPath $layoutDoc -Raw
foreach ($marker in @('desktop/shell', 'desktop/wallpaper', 'desktop/widgets', 'ai/pi', 'ui/wallpaper', 'Public headers')) {
    if (-not $doc.Contains($marker)) { throw "Native source layout documentation missing marker: $marker" }
}

Write-Host 'Native source layout OK: implementation files are organized by process/domain and CMake mirrors the physical tree.'
