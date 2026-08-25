param()

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

function Require-Text([string]$relativePath) {
    $path = Join-Path $root $relativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Missing M4 desktop UI parity input: $relativePath"
    }
    return Get-Content -LiteralPath $path -Raw
}

$legacy = Require-Text 'src/native/src/ui/wallpaper/WallpaperLibraryWindow.cpp'
$candidate = Require-Text 'src/native/src/ui/wallpaper/WallpaperLibraryWindowV2.cpp'
$candidateBridge = Require-Text 'src/native/src/ui/wallpaper/WallpaperLibraryWindowV2Candidate.cpp'
$production = Require-Text 'src/native/src/ui/wallpaper/WallpaperLibraryWindowProduction.cpp'
$header = Require-Text 'src/native/include/turingdesk/WallpaperLibraryWindow.h'
$cmake = Require-Text 'src/native/CMakeLists.txt'

# The shipping UI is still the compatibility-wrapped legacy window. Keep its
# current capability surface explicit until the V2 shell reaches parity.
foreach ($marker in @(
    'kAiUrlId',
    'kAiKeyId',
    'kAiModelId',
    'kAiProbeId',
    'kAiSaveId',
    'kAiHarnessId',
    'kAiClearKeyId',
    'kWidgetCreateClockId',
    'kWidgetToggleId',
    'kWidgetRemoveId',
    'kWidgetRefreshId',
    'WallpaperSettingsSection::Playlists',
    'WallpaperSettingsSection::Displays',
    'WallpaperSettingsSection::Rules',
    'WallpaperSettingsSection::Performance',
    'WallpaperSettingsSection::AI')) {
    if (-not $legacy.Contains($marker)) {
        throw "Shipping desktop UI lost required parity marker: $marker"
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

# M4 candidate contract: vertical product shell, card surfaces and explicit
# delegation to the existing product sections that have not yet been moved into
# the shell. These are implementation markers, not user-visible strings.
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
        throw "M4 candidate lost required shell/parity marker: $marker"
    }
}

# Once a candidate crosses a domain boundary correctly, it must never regress
# simply because it is not shipping yet.
foreach ($forbidden in @(
    '#include "turingdesk/DesktopWidgetStore.h"',
    'DesktopWidgetStore store',
    'WritePrivateProfileStringW',
    'GetPrivateProfileStringW',
    'FindWindowW(L"Progman"',
    'SetParent(')) {
    if ($candidate.Contains($forbidden)) {
        throw "M4 candidate regained forbidden persistence/shell ownership: $forbidden"
    }
}

foreach ($forbidden in @(
    '#define DesktopWidgetStore',
    '#define max(')) {
    if ($candidateBridge.Contains($forbidden)) {
        throw "M4 candidate compile bridge regained a compatibility interception shim: $forbidden"
    }
}
if (-not $candidateBridge.Contains('#include "WallpaperLibraryWindowV2.cpp"')) {
    throw 'M4 candidate bridge must compile the real V2 source.'
}

$productionUsesV2 = $production.Contains('WallpaperLibraryWindowV2.cpp')
$productionUsesLegacy = $production.Contains('WallpaperLibraryWindow.cpp')
if ($productionUsesV2 -and $productionUsesLegacy) {
    throw 'Production bridge must select exactly one WallpaperLibraryWindow implementation.'
}
if (-not $productionUsesV2 -and -not $productionUsesLegacy) {
    throw 'Production bridge must compile either the guarded legacy UI or the parity-complete V2 UI.'
}

if (-not $cmake.Contains('src/ui/wallpaper/WallpaperLibraryWindowProduction.cpp')) {
    throw 'Production target must remain routed through WallpaperLibraryWindowProduction.cpp.'
}
if ($cmake.Contains('src/ui/wallpaper/WallpaperLibraryWindowV2.cpp')) {
    throw 'Do not compile WallpaperLibraryWindowV2.cpp directly into production; switch only through the parity bridge.'
}
if (-not $cmake.Contains('WallpaperLibraryWindowV2Candidate.cpp')) {
    throw 'M4 V2 candidate must compile in normal Windows builds so parity work cannot silently rot.'
}

if ($productionUsesV2) {
    # Shipping V2 additionally requires the candidate's existing delegated
    # product sections and controller boundaries to remain present. The current
    # legacy production bridge stays active until a deliberate switch commit.
    foreach ($marker in @(
        'DesktopWidgetController',
        'WallpaperSettingsSection::AI',
        'WallpaperSettingsSection::Playlists',
        'WallpaperSettingsSection::Displays',
        'WallpaperSettingsSection::Rules',
        'WallpaperSettingsSection::Performance')) {
        if (-not $candidate.Contains($marker)) {
            throw "M4 production candidate missing required parity marker: $marker"
        }
    }
}

Write-Host 'Desktop UI parity contract OK: legacy production capabilities are preserved; V2 is vertical-shell, controller-routed, continuously compiled, and cannot regress to private store/shell ownership.'
