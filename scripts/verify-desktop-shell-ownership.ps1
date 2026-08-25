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
$diagnosticsHeaderPath = Require-File 'src/native/include/turingdesk/DesktopShellDiagnostics.h'
$diagnosticsSourcePath = Require-File 'src/native/src/DesktopShellDiagnostics.cpp'
$legacyEnginePath = Require-File 'src/native/src/WallpaperEngine.cpp'
$coordinatorPath = Require-File 'src/native/src/WallpaperWebRuntimeCoordinator.cpp'
$cmakePath = Require-File 'src/native/CMakeLists.txt'

$shellHeader = Get-Content -LiteralPath $shellHeaderPath -Raw
$shellSource = Get-Content -LiteralPath $shellSourcePath -Raw
$diagnosticsHeader = Get-Content -LiteralPath $diagnosticsHeaderPath -Raw
$diagnosticsSource = Get-Content -LiteralPath $diagnosticsSourcePath -Raw
$legacyEngine = Get-Content -LiteralPath $legacyEnginePath -Raw
$coordinator = Get-Content -LiteralPath $coordinatorPath -Raw
$cmake = Get-Content -LiteralPath $cmakePath -Raw

foreach ($marker in @('DesktopShellHost', 'AttachSurface', 'EnsureCurrent', 'InspectSurface', 'DesktopShellSnapshot')) {
    if (-not $shellHeader.Contains($marker)) { throw "DesktopShellHost header missing contract marker: $marker" }
}
foreach ($marker in @('0x052C', 'FindWindowW(kProgmanClass', 'FindWindowExW', 'RequestWallpaperLayer', 'RepairRaisedDesktopWorkerOrder', 'RepairKnownTuringDeskSurfaces')) {
    if (-not $shellSource.Contains($marker)) { throw "DesktopShellHost no longer owns required shell behavior: $marker" }
}
foreach ($marker in @('shellMode', 'parentValid', 'layeredRequired', 'layeredApplied', 'zOrderValid', 'visible', 'lastError')) {
    if (-not $diagnosticsHeader.Contains($marker)) { throw "Desktop attachment diagnostics missing marker: $marker" }
}
foreach ($marker in @('InspectDesktopAttachment', 'DescribeDesktopAttachment', 'DesktopShellHost::ModeKey', 'ZOrderValid')) {
    if (-not $diagnosticsSource.Contains($marker)) { throw "Desktop attachment diagnostics implementation missing marker: $marker" }
}
if (-not $cmake.Contains('src/DesktopShellDiagnostics.cpp')) {
    throw 'TuringDeskWallpaper must compile DesktopShellDiagnostics.cpp.'
}

# M2 migration rule: renderer/coordinator/Widget code may inspect or locate its
# own TuringDesk child windows, but it must not discover Windows shell classes,
# send the WorkerW creation message, or directly re-parent a desktop surface.
# WallpaperEngine.cpp is the one tracked legacy shell-discovery exception until
# its AttachToDesktop path is replaced with DesktopShellHost.
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

# Generic FindWindowExW is intentionally allowed because WebWallpaperHost uses
# it to locate its own TuringDesk.Native.WebWallpaperHost child by token. That
# operation is process/surface lifecycle, not Progman/WorkerW discovery.
if (-not $coordinator.Contains('MaintainDesktopSurfaceZOrder')) {
    throw 'M2 coordinator z-order exception changed unexpectedly; migrate it intentionally through DesktopShellHost.'
}

# Keep the WallpaperEngine temporary shell-discovery exception visible until a
# later M2 wave removes it. Once removed, delete these markers and require the
# engine to own a DesktopShellHost instance instead.
foreach ($marker in @('DesktopLayer DiscoverDesktopLayer()', 'SpawnWallpaperLayer(', 'FindWindowW(L"Progman"')) {
    if (-not $legacyEngine.Contains($marker)) {
        throw "M2 legacy-shell exception changed unexpectedly; update the migration guard intentionally: $marker"
    }
}

Write-Host 'Desktop shell ownership contract OK (WallpaperEngine discovery + coordinator z-order exceptions tracked).'
