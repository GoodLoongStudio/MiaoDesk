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

$paths = @{
    ShellHeader = 'src/native/include/turingdesk/DesktopShellHost.h'
    Shell = 'src/native/src/desktop/shell/DesktopShellHost.cpp'
    Stack = 'src/native/src/desktop/shell/DesktopShellSurfaceStack.cpp'
    DiagnosticsHeader = 'src/native/include/turingdesk/DesktopShellDiagnostics.h'
    Diagnostics = 'src/native/src/desktop/shell/DesktopShellDiagnostics.cpp'
    MonitorLayout = 'src/native/src/desktop/wallpaper/monitor/WallpaperMonitorLayout.cpp'
    LegacyEngine = 'src/native/src/desktop/wallpaper/legacy/WallpaperEngine.cpp'
    ProductionEngine = 'src/native/src/desktop/wallpaper/legacy/WallpaperEngineProduction.cpp'
    Coordinator = 'src/native/src/desktop/wallpaper/web/WallpaperWebRuntimeCoordinator.cpp'
    CMake = 'src/native/CMakeLists.txt'
}
$text = @{}
foreach ($entry in $paths.GetEnumerator()) {
    $text[$entry.Key] = Get-Content -LiteralPath (Require-File $entry.Value) -Raw
}

foreach ($marker in @('DesktopShellHost', 'AttachSurface', 'EnsureSurface', 'EnsureCurrent', 'InspectSurface', 'RecoverSurface', 'CurrentGenerationValid')) {
    if (-not $text.ShellHeader.Contains($marker)) { throw "DesktopShellHost header missing marker: $marker" }
}
foreach ($marker in @('0x052C', 'FindWindowW(kProgmanClass', 'FindWindowExW', 'RequestWallpaperLayer', 'RepairRaisedDesktopWorkerOrder', 'RepairKnownTuringDeskSurfaces')) {
    if (-not $text.Shell.Contains($marker)) { throw "DesktopShellHost no longer owns required shell behavior: $marker" }
}
foreach ($marker in @('DesktopShellHost::EnsureSurface', 'DesktopShellHost::RepairSurfaceStack', 'DesktopShellHost::RecoverSurface', 'DesktopShellHost::CurrentGenerationValid', 'AttachSurface(surface')) {
    if (-not $text.Stack.Contains($marker)) { throw "Desktop shell recovery contract missing marker: $marker" }
}
foreach ($marker in @('shellMode', 'parentValid', 'layeredRequired', 'layeredApplied', 'zOrderValid', 'visible', 'lastError')) {
    if (-not $text.DiagnosticsHeader.Contains($marker)) { throw "Desktop attachment diagnostics header missing marker: $marker" }
}
foreach ($marker in @('InspectDesktopAttachment', 'DescribeDesktopAttachment', 'DesktopShellHost::ModeKey', 'ZOrderValid')) {
    if (-not $text.Diagnostics.Contains($marker)) { throw "Desktop attachment diagnostics source missing marker: $marker" }
}
foreach ($marker in @('topology.virtualBounds = {-1920, -240, 3840, 2160}', 'DrawRegionsInHost(topology, LayoutMode::Clone)', 'LayoutMode::Independent')) {
    if (-not $text.MonitorLayout.Contains($marker)) { throw "Monitor geometry self-test missing marker: $marker" }
}

$cmake = $text.CMake
foreach ($marker in @(
    'src/desktop/shell/DesktopShellHost.cpp',
    'src/desktop/shell/DesktopShellDiagnostics.cpp',
    'src/desktop/shell/DesktopShellSurfaceStack.cpp',
    'src/desktop/wallpaper/legacy/WallpaperEngineProduction.cpp',
    'src/desktop/wallpaper/web/WallpaperWebRuntimeCoordinator.cpp')) {
    if (-not $cmake.Contains($marker)) { throw "Desktop shell production source missing from CMake: $marker" }
}
if ($cmake.Contains('WallpaperWebRuntimeCoordinatorProduction.cpp')) {
    throw 'Transitional Web coordinator production bridge must not return.'
}

$surfaceSources = @(
    'src/native/src/desktop/wallpaper/monitor/IndependentWallpaperHost.cpp',
    'src/native/src/desktop/wallpaper/web/WebWallpaperHost.cpp',
    'src/native/src/desktop/wallpaper/web/WebDesktopSurfaceChild.cpp',
    'src/native/src/desktop/wallpaper/web/WallpaperWebRuntimeCoordinator.cpp',
    'src/native/src/desktop/widgets/DesktopWidgetController.cpp',
    'src/native/src/ui/widgets/DesktopWidgetUiAdapter.cpp',
    'src/native/src/desktop/wallpaper/render/VideoWallpaperSet.cpp'
)
$forbiddenShellTokens = @('0x052C', 'FindWindowW(L"Progman"', 'L"Progman"', 'L"WorkerW"', 'L"SHELLDLL_DefView"', 'SetParent(')
foreach ($relativePath in $surfaceSources) {
    $surfaceText = Get-Content -LiteralPath (Require-File $relativePath) -Raw
    foreach ($token in $forbiddenShellTokens) {
        if ($surfaceText.Contains($token)) { throw "$relativePath regained Windows desktop attachment ownership: $token" }
    }
}

foreach ($marker in @(
    'DesktopShellHost shellHost',
    'shellHost.EnsureCurrent',
    'shellHost.InspectSurface',
    'shellHost.RecoverSurface',
    'shellHost.SurfaceParent',
    'shellHost.RepairSurfaceStack',
    'shellHost.EnsureSurface(host, DesktopSurfaceRole::Wallpaper',
    'HostDesktopBounds(topology, LayoutMode::Independent)',
    'IsWindowVisible(host)')) {
    if (-not $text.Coordinator.Contains($marker)) { throw "Web runtime coordinator missing DesktopShellHost routing marker: $marker" }
}
foreach ($forbidden in @(
    'MaintainDesktopSurfaceZOrder',
    'DesktopAnchorAboveHost',
    'IsWebSurface(',
    'GetWindow(parent, GW_CHILD)',
    'EnsureIndependentHostBounds',
    'DesktopRectToParentClient(',
    'SetWindowPos(')) {
    if ($text.Coordinator.Contains($forbidden)) { throw "Web runtime coordinator regained shell/geometry ownership: $forbidden" }
}

foreach ($marker in @(
    'DesktopShellHost& ProductionShellHost()',
    'TuringDeskFindWindowW',
    'TuringDeskFindWindowExW',
    'TuringDeskSendMessageTimeoutW',
    'TuringDeskSetParent',
    'TuringDeskSetWindowPos',
    'shell.EnsureSurface(child',
    'shell.EnsureSurface(window',
    '#define FindWindowW TuringDeskFindWindowW',
    '#define FindWindowExW TuringDeskFindWindowExW',
    '#define SendMessageTimeoutW TuringDeskSendMessageTimeoutW',
    '#define SetParent TuringDeskSetParent',
    '#define SetWindowPos TuringDeskSetWindowPos')) {
    if (-not $text.ProductionEngine.Contains($marker)) { throw "Production WallpaperEngine shell interception missing marker: $marker" }
}
foreach ($forbidden in @('::SetWindowPos(window, nullptr', 'shell.AttachSurface(child')) {
    if ($text.ProductionEngine.Contains($forbidden)) { throw "Production WallpaperEngine regained partial attachment ownership: $forbidden" }
}

foreach ($marker in @('DesktopLayer DiscoverDesktopLayer()', 'SpawnWallpaperLayer(', 'FindWindowW(L"Progman"')) {
    if (-not $text.LegacyEngine.Contains($marker)) { throw "M2 legacy source-cleanup exception changed unexpectedly: $marker" }
}

Write-Host 'Desktop shell ownership contract OK.'
