param()

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

function Require-Text([string]$relativePath) {
    $path = Join-Path $root $relativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Missing desktop UI parity input: $relativePath"
    }
    return Get-Content -LiteralPath $path -Raw
}

$ui = Require-Text 'src/native/src/ui/wallpaper/WallpaperLibraryWindowV2.cpp'
$production = Require-Text 'src/native/src/ui/wallpaper/WallpaperLibraryWindowProduction.cpp'
$header = Require-Text 'src/native/include/miaodesk/WallpaperLibraryWindow.h'
$cmake = Require-Text 'src/native/CMakeLists.txt'
$legacyPath = Join-Path $root 'src/native/src/ui/wallpaper/WallpaperLibraryWindow.cpp'
$candidatePath = Join-Path $root 'src/native/src/ui/wallpaper/WallpaperLibraryWindowV2Candidate.cpp'

# Exactly one Desktop Library implementation may exist. The shipping wrapper
# must stay include-only and must never add a second geometry/navigation pass.
if (Test-Path -LiteralPath $legacyPath -PathType Leaf) {
    throw 'Retired legacy WallpaperLibraryWindow.cpp must not return.'
}
if (Test-Path -LiteralPath $candidatePath -PathType Leaf) {
    throw 'Retired compile-only WallpaperLibraryWindowV2Candidate.cpp must not return.'
}
if (-not $production.Contains('#include "WallpaperLibraryWindowV2.cpp"')) {
    throw 'Production Desktop Library must compile the single V2 UI.'
}
foreach ($forbidden in @(
    'SetWindowSubclass',
    'SetWinEventHook',
    'ApplyCompactDesktopNavigation',
    'ApplyResponsiveDesktopLayout',
    'DesktopResizeHitTest',
    'MoveWindow(',
    'ShowWindow(',
    '#define DesktopWidgetStore',
    '#include "miaodesk/DesktopWidgetStore.h"',
    'WritePrivateProfileStringW',
    'GetPrivateProfileStringW',
    'FindWindowW(L"Progman"',
    'SetParent(')) {
    if ($production.Contains($forbidden)) {
        throw "Production Desktop Library wrapper regained forbidden ownership: $forbidden"
    }
}

# Keep the domain enum stable while exposing only the current three UI entries.
if (-not $header.Contains('enum class WallpaperSettingsSection')) {
    throw 'WallpaperLibraryWindow contract lost WallpaperSettingsSection enum.'
}
foreach ($marker in @('Installed,', 'Widgets,', 'AI,')) {
    if (-not $header.Contains($marker)) {
        throw "WallpaperLibraryWindow contract lost active navigation section: $marker"
    }
}

# Shipping V2 creates exactly three nav controls. Do not regress to hidden
# placeholder navigation for unfinished product sections.
foreach ($marker in @(
    'kNavInstalledId',
    'kNavWidgetsId',
    'kNavAiId',
    'std::array<HWND, 3> nav',
    'std::array<const wchar_t*, 3> navLabels',
    'std::array<int, 3> navIds',
    'kWallpaperGridId',
    'kWidgetGridId',
    'kSearchId',
    'kAddId',
    'DesktopWidgetController',
    'widgetController.RuntimeHealth',
    'WallpaperSettingsSection::AI')) {
    if (-not $ui.Contains($marker)) {
        throw "Production Desktop UI lost required single-shell marker: $marker"
    }
}
foreach ($forbidden in @(
    'kNavPlaylistsId',
    'kNavDisplaysId',
    'kNavRulesId',
    'kNavPerformanceId',
    '#include "miaodesk/DesktopWidgetStore.h"',
    'DesktopWidgetStore store',
    'WritePrivateProfileStringW',
    'GetPrivateProfileStringW',
    'FindWindowW(L"Progman"',
    'SetParent(')) {
    if ($ui.Contains($forbidden)) {
        throw "Production Desktop UI regained retired/forbidden marker: $forbidden"
    }
}

if (-not $cmake.Contains('src/ui/wallpaper/WallpaperLibraryWindowProduction.cpp')) {
    throw 'Production target must remain routed through WallpaperLibraryWindowProduction.cpp.'
}
if ($cmake.Contains('src/ui/wallpaper/WallpaperLibraryWindow.cpp')) {
    throw 'Retired legacy Desktop Library source must not be present in production build graph.'
}
if ($cmake.Contains('src/ui/wallpaper/WallpaperLibraryWindowV2.cpp')) {
    throw 'Compile V2 through the production translation unit so there is one production entry.'
}
if ($cmake.Contains('WallpaperLibraryWindowV2Candidate.cpp') -or
    $cmake.Contains('MiaoDeskDesktopUiV2Candidate')) {
    throw 'Retired duplicate V2 candidate target must not remain in build graph.'
}

Write-Host 'Desktop UI contract OK: one V2 production window, three real nav controls, no hidden placeholder nav, no duplicate candidate target, no second production layout wrapper.'
