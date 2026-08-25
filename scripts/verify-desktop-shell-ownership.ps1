param()

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

function Require-File([string]$relativePath) {
    $path = Join-Path $root $relativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Missing desktop shell ownership file: $relativePath"
    }
    return $path
}

$shellHeaderPath = Require-File 'src/native/include/turingdesk/DesktopShellHost.h'
$shellSourcePath = Require-File 'src/native/src/DesktopShellHost.cpp'
$surfaceStackPath = Require-File 'src/native/src/DesktopShellSurfaceStack.cpp'
$diagnosticsHeaderPath = Require-File 'src/native/include/turingdesk/DesktopShellDiagnostics.h'
$diagnosticsSourcePath = Require-File 'src/native/src/DesktopShellDiagnostics.cpp'
$monitorLayoutPath = Require-File 'src/native/src/WallpaperMonitorLayout.cpp'
$legacyEnginePath = Require-File 'src/native/src/WallpaperEngine.cpp'
$productionEnginePath = Require-File 'src/native/src/WallpaperEngineProduction.cpp'
$coordinatorPath = Require-File 'src/native/src/WallpaperWebRuntimeCoordinator.cpp'
$cmakePath = Require-File 'src/native/CMakeLists.txt'

$shellHeader = Get-Content -LiteralPath $shellHeaderPath -Raw
$shellSource = Get-Content -LiteralPath $shellSourcePath -Raw
$surfaceStack = Get-Content -LiteralPath $surfaceStackPath -Raw
$diagnosticsHeader = Get-Content -LiteralPath $diagnosticsHeaderPath -Raw
$diagnosticsSource = Get-Content -LiteralPath $diagnosticsSourcePath -Raw
$monitorLayout = Get-Content -LiteralPath $monitorLayoutPath -Raw
$legacyEngine = Get-Content -LiteralPath $legacyEnginePath -Raw
$productionEngine = Get-Content -LiteralPath $productionEnginePath -Raw
$coordinator = Get-Content -LiteralPath $coordinatorPath -Raw
$cmake = Get-Content -LiteralPath $cmakePath -Raw

foreach ($marker in @('DesktopShellHost', 'AttachSurface', 'EnsureCurrent', 'InspectSurface', 'DesktopShellSnapshot', 'RecoverSurface', 'CurrentGenerationValid')) {
    if (-not $shellHeader.Contains($marker)) { throw "DesktopShellHost header missing contract marker: $marker" }
}
foreach ($marker in @('0x052C', 'FindWindowW(kProgmanClass', 'FindWindowExW', 'RequestWallpaperLayer', 'RepairRaisedDesktopWorkerOrder', 'RepairKnownTuringDeskSurfaces')) {
    if (-not $shellSource.Contains($marker)) { throw "DesktopShellHost no longer owns required shell behavior: $marker" }
}
foreach ($marker in @('DesktopShellHost::RepairSurfaceStack', 'DesktopShellHost::RecoverSurface', 'DesktopShellHost::CurrentGenerationValid', 'FindWindowW(L"Progman"', 'AttachSurface(surface')) {
    if (-not $surfaceStack.Contains($marker)) { throw "Desktop shell recovery contract missing marker: $marker" }
}
foreach ($marker in @('shellMode', 'parentValid', 'layeredRequired', 'layeredApplied', 'zOrderValid', 'visible', 'lastError')) {
    if (-not $diagnosticsHeader.Contains($marker)) { throw "Desktop attachment diagnostics missing marker: $marker" }
}
foreach ($marker in @('InspectDesktopAttachment', 'DescribeDesktopAttachment', 'DesktopShellHost::ModeKey', 'ZOrderValid')) {
    if (-not $diagnosticsSource.Contains($marker)) { throw "Desktop attachment diagnostics implementation missing marker: $marker" }
}
foreach ($marker in @('topology.virtualBounds = {-1920, -240, 3840, 2160}', 'DrawRegionsInHost(topology, LayoutMode::Clone)', 'LayoutMode::Independent')) {
    if (-not $monitorLayout.Contains($marker)) { throw "Negative-coordinate / mixed-monitor geometry self-test missing marker: $marker" }
}
if (-not $cmake.Contains('src/DesktopShellDiagnostics.cpp')) {
    throw 'TuringDeskWallpaper must compile DesktopShellDiagnostics.cpp.'
}
if (-not $cmake.Contains('src/DesktopShellSurfaceStack.cpp')) {
    throw 'TuringDeskWallpaper must compile DesktopShellSurfaceStack.cpp.'
}
if (-not $cmake.Contains('src/WallpaperEngineProduction.cpp')) {
    throw 'Production target must compile WallpaperEngineProduction.cpp.'
}

# M2 migration rule: renderer/coordinator/Widget code may inspect or locate its
# own TuringDesk child windows, but it must not discover Windows shell classes,
# send the WorkerW creation message, or directly re-parent a desktop surface.
$surfaceSources = @(
    'src/native/src/IndependentWallpaperHost.cpp',
    'src/native/src/WebWallpaperHost.cpp',
    'src/native/src/WebDesktopSurfaceChild.cpp',
    'src/native/src/WallpaperWebRuntimeCoordinator.cpp',
    'src/native/src/DesktopWidgetController.cpp',
    'src/native/src/DesktopWidgetUiAdapter.cpp',
    'src/native/src/VideoWallpaperSet.cpp'
)
$forbiddenShellTokens = @(
    '0x052C',
    'FindWindowW(L"Progman"',
    'L"Progman"',
    'L"WorkerW"',
    'L"SHELLDLL_DefView"',
    'SetParent('
)
foreach ($relativePath in $surfaceSources) {
    $path = Require-File $relativePath
    $text = Get-Content -LiteralPath $path -Raw
    foreach ($token in $forbiddenShellTokens) {
        if ($text.Contains($token)) {
            throw "$relativePath regained Windows desktop attachment ownership: $token"
        }
    }
}

# The Web/Widget coordinator used to enumerate siblings and maintain its own
# z-order. That exception is closed: parent validation, stale-parent recovery
# and stack repair all route through DesktopShellHost.
foreach ($marker in @('DesktopShellHost shellHost', 'shellHost.EnsureCurrent', 'shellHost.InspectSurface', 'shellHost.RecoverSurface', 'shellHost.SurfaceParent', 'shellHost.RepairSurfaceStack')) {
    if (-not $coordinator.Contains($marker)) {
        throw "Web runtime coordinator missing DesktopShellHost routing marker: $marker"
    }
}
foreach ($forbidden in @('MaintainDesktopSurfaceZOrder', 'DesktopAnchorAboveHost', 'IsWebSurface(', 'GetWindow(parent, GW_CHILD)')) {
    if ($coordinator.Contains($forbidden)) {
        throw "Web runtime coordinator regained sibling/z-order ownership: $forbidden"
    }
}

# WallpaperEngine.cpp still contains legacy source text while M2 removes it in
# stages. The production bridge must intercept every shell API used by that code,
# so no production execution can independently discover Progman/WorkerW, send
# 0x052C, re-parent the host, or impose desktop z-order outside DesktopShellHost.
foreach ($marker in @(
    'DesktopShellHost& ProductionShellHost()',
    'TuringDeskFindWindowW',
    'TuringDeskFindWindowExW',
    'TuringDeskSendMessageTimeoutW',
    'TuringDeskSetParent',
    'TuringDeskSetWindowPos',
    'shell.AttachSurface(child',
    'ProductionShellHost().RepairSurfaceStack',
    '#define FindWindowW TuringDeskFindWindowW',
    '#define FindWindowExW TuringDeskFindWindowExW',
    '#define SendMessageTimeoutW TuringDeskSendMessageTimeoutW',
    '#define SetParent TuringDeskSetParent',
    '#define SetWindowPos TuringDeskSetWindowPos')) {
    if (-not $productionEngine.Contains($marker)) {
        throw "Production WallpaperEngine shell interception missing marker: $marker"
    }
}

# Keep the source-cleanup exception explicit until the legacy discovery helpers
# themselves are deleted. At runtime this code is intercepted as verified above;
# the remaining task is source removal, not a second production shell owner.
foreach ($marker in @('DesktopLayer DiscoverDesktopLayer()', 'SpawnWallpaperLayer(', 'FindWindowW(L"Progman"')) {
    if (-not $legacyEngine.Contains($marker)) {
        throw "M2 legacy source-cleanup exception changed unexpectedly; update guard intentionally: $marker"
    }
}

Write-Host 'Desktop shell ownership contract OK (coordinator centralized; production engine shell APIs intercepted; legacy engine source cleanup remains).'
