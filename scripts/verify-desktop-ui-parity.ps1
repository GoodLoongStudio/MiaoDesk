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

foreach ($marker in @(
    'WallpaperSettingsSection::Installed',
    'WallpaperSettingsSection::Widgets',
    'WallpaperSettingsSection::Playlists',
    'WallpaperSettingsSection::Displays',
    'WallpaperSettingsSection::Rules',
    'WallpaperSettingsSection::Performance',
    'WallpaperSettingsSection::AI')) {
    if (-not $header.Contains($marker)) {
        throw "WallpaperLibraryWindow contract lost navigation section: $marker"
    }
}

# The candidate must keep the intended product-shell information architecture
# visible while it is developed behind the production bridge.
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
    'SectionForNav')) {
    if (-not $candidate.Contains($marker)) {
        throw "M4 candidate lost required navigation/layout marker: $marker"
    }
}

$productionUsesV2 = $production.Contains('WallpaperLibraryWindowV2.cpp')
$productionUsesLegacy = $production.Contains('WallpaperLibraryWindow.cpp')

if ($productionUsesV2) {
    # Hard gate: V2 may not become the shipping window while it still bypasses
    # domain boundaries or omits the old AI/advanced-workbench surface.
    foreach ($forbidden in @(
        '#include "turingdesk/DesktopWidgetStore.h"',
        'DesktopWidgetStore store')) {
        if ($candidate.Contains($forbidden)) {
            throw "M4 candidate cannot become production while it bypasses Widget domain ownership: $forbidden"
        }
    }

    if (-not ($candidate.Contains('DesktopWidgetController') -or $candidate.Contains('DesktopControlService'))) {
        throw 'M4 candidate cannot become production until Widget actions route through a controller/service.'
    }

    foreach ($marker in @(
        'WallpaperSettingsSection::AI',
        'WallpaperSettingsSection::Playlists',
        'WallpaperSettingsSection::Displays',
        'WallpaperSettingsSection::Rules',
        'WallpaperSettingsSection::Performance')) {
        if (-not $candidate.Contains($marker)) {
            throw "M4 production candidate missing delegated product section: $marker"
        }
    }

    if ($productionUsesLegacy) {
        throw 'Production bridge must select exactly one WallpaperLibraryWindow implementation.'
    }
} elseif (-not $productionUsesLegacy) {
    throw 'Production bridge must compile either the guarded legacy UI or the parity-complete V2 UI.'
}

if (-not $cmake.Contains('src/ui/wallpaper/WallpaperLibraryWindowProduction.cpp')) {
    throw 'Production target must remain routed through WallpaperLibraryWindowProduction.cpp.'
}
if ($cmake.Contains('src/ui/wallpaper/WallpaperLibraryWindowV2.cpp')) {
    throw 'Do not compile WallpaperLibraryWindowV2.cpp directly into production; switch only through the parity bridge.'
}

# Candidate debt is reported but does not block development while V2 is not the
# production implementation. The production-switch checks above turn the same
# debt into a hard error when somebody attempts to ship V2 prematurely.
$debt = @()
if ($candidate.Contains('DesktopWidgetStore store') -or $candidate.Contains('#include "turingdesk/DesktopWidgetStore.h"')) {
    $debt += 'Widget CRUD still bypasses DesktopWidgetController/DesktopControlService'
}
if (-not $candidate.Contains('kNavAiId')) { $debt += 'AI navigation missing' }

if ($debt.Count -gt 0) {
    Write-Host ('M4 candidate debt: ' + ($debt -join '; '))
}

Write-Host 'Desktop UI parity contract OK: production capabilities are preserved and V2 cannot ship before domain/parity gates pass.'
