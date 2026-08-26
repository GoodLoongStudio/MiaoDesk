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

$candidate = Require-Text 'src/native/src/ui/wallpaper/WallpaperLibraryWindowV2.cpp'
$candidateBridge = Require-Text 'src/native/src/ui/wallpaper/WallpaperLibraryWindowV2Candidate.cpp'
$production = Require-Text 'src/native/src/ui/wallpaper/WallpaperLibraryWindowProduction.cpp'
$header = Require-Text 'src/native/include/turingdesk/WallpaperLibraryWindow.h'
$cmake = Require-Text 'src/native/CMakeLists.txt'
$legacyPath = Join-Path $root 'src/native/src/ui/wallpaper/WallpaperLibraryWindow.cpp'

# There is now exactly one Desktop Library UI implementation. The retired
# legacy source must not return because a second production-looking UI makes
# real-Windows validation ambiguous.
if (Test-Path -LiteralPath $legacyPath -PathType Leaf) {
    throw 'Retired legacy WallpaperLibraryWindow.cpp must not return.'
}
if (-not $production.Contains('#include "WallpaperLibraryWindowV2.cpp"')) {
    throw 'Production Desktop Library must compile the V2 UI.'
}
if ($production.Contains('WallpaperLibraryWindow.cpp') -and -not $production.Contains('WallpaperLibraryWindowV2.cpp')) {
    throw 'Production Desktop Library regressed to the retired legacy UI.'
}
foreach ($forbidden in @(
    '#define DesktopWidgetStore',
    '#include "turingdesk/DesktopWidgetStore.h"',
    'WritePrivateProfileStringW',
    'GetPrivateProfileStringW',
    'FindWindowW(L"Progman"',
    'SetParent(')) {
    if ($production.Contains($forbidden)) {
        throw "Production V2 bridge regained forbidden compatibility/store/shell ownership: $forbidden"
    }
}

if (-not $header.Contains('enum class WallpaperSettingsSection')) {
    throw 'WallpaperLibraryWindow contract lost WallpaperSettingsSection enum.'
}
foreach ($marker in @('Installed,', 'Widgets,', 'Playlists,', 'Displays,', 'Rules,', 'Performance,', 'AI,')) {
    if (-not $header.Contains($marker)) {
        throw "WallpaperLibraryWindow contract lost navigation section: $marker"
    }
}

# V2 production shell contract. Sections not yet rendered natively inside the
# shell stay reachable through NavigateCallback; Widget state goes through the
# controller/domain boundary rather than a private Store.
foreach ($marker in @(
    'kNavInstalledId',
    'kNavWidgetsId',
    'kNavPlaylistsId',
    'kNavDisplaysId',
    'kNavRulesId',
    'kNavPerformanceId',
    'kNavAiId',
    'kWallpaperGridId',
    'kWidgetGridId',
    'kSearchId',
    'kAddId',
    'SectionForNav',
    'const int sidebarW',
    'DesktopWidgetController',
    'widgetController.RuntimeHealth',
    'WallpaperSettingsSection::AI',
    'WallpaperSettingsSection::Playlists',
    'WallpaperSettingsSection::Displays',
    'WallpaperSettingsSection::Rules',
    'WallpaperSettingsSection::Performance')) {
    if (-not $candidate.Contains($marker)) {
        throw "Production V2 lost required shell/parity marker: $marker"
    }
}

foreach ($forbidden in @(
    '#include "turingdesk/DesktopWidgetStore.h"',
    'DesktopWidgetStore store',
    'WritePrivateProfileStringW',
    'GetPrivateProfileStringW',
    'FindWindowW(L"Progman"',
    'SetParent(')) {
    if ($candidate.Contains($forbidden)) {
        throw "Production V2 regained forbidden persistence/shell ownership: $forbidden"
    }
}

foreach ($forbidden in @('#define DesktopWidgetStore', '#define max(')) {
    if ($candidateBridge.Contains($forbidden)) {
        throw "V2 compile bridge regained a compatibility interception shim: $forbidden"
    }
}
if (-not $candidateBridge.Contains('#include "WallpaperLibraryWindowV2.cpp"')) {
    throw 'V2 candidate bridge must compile the same V2 source as production.'
}

if (-not $cmake.Contains('src/ui/wallpaper/WallpaperLibraryWindowProduction.cpp')) {
    throw 'Production target must remain routed through WallpaperLibraryWindowProduction.cpp.'
}
if ($cmake.Contains('src/ui/wallpaper/WallpaperLibraryWindow.cpp')) {
    throw 'Retired legacy Desktop Library source must not be present in the production build graph.'
}
if ($cmake.Contains('src/ui/wallpaper/WallpaperLibraryWindowV2.cpp')) {
    throw 'Compile V2 through the production translation unit so there remains one production entry.'
}
if (-not $cmake.Contains('WallpaperLibraryWindowV2Candidate.cpp')) {
    throw 'V2 candidate target must continue compiling the same source independently as a parity/build check.'
}

Write-Host 'Desktop UI parity contract OK: V2 is the sole production Desktop Library UI; the retired legacy window is absent, Widget state remains controller-routed, and delegated product sections remain reachable while M4 parity work continues.'
