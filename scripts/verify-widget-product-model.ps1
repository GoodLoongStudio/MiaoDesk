param()

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

$header = Join-Path $root 'src/native/include/turingdesk/DesktopWidgetController.h'
$controller = Join-Path $root 'src/native/src/desktop/widgets/DesktopWidgetController.cpp'
$acceptance = Join-Path $root 'src/native/src/desktop/widgets/WidgetRuntimeAcceptanceMain.cpp'
$configContinuity = Join-Path $root 'src/native/src/desktop/widgets/WidgetAcceptanceConfigContinuity.cpp'
$surfaceChild = Join-Path $root 'src/native/src/desktop/wallpaper/web/WebDesktopSurfaceChild.cpp'
$doc = Join-Path $root 'docs/WIDGET_PRODUCT_MODEL_M3.md'

foreach ($path in @($header, $controller, $acceptance, $configContinuity, $surfaceChild, $doc)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Missing Widget product-model contract input: $path"
    }
}

$headerText = Get-Content -LiteralPath $header -Raw
foreach ($marker in @(
    'enum class WidgetFixedPreset',
    'GlassClock',
    'TodayTasks',
    'WeatherGlass',
    'CreatePreset',
    'MoveTo',
    'SetEnabled',
    'Remove')) {
    if (-not $headerText.Contains($marker)) {
        throw "Fixed-format Widget controller contract missing marker: $marker"
    }
}
foreach ($forbidden in @('WidgetSizePreset', 'SetSize(', 'MoveToMonitor(')) {
    if ($headerText.Contains($forbidden)) {
        throw "M3 fixed-format Widget surface must not expose deferred resize/monitor-edit API: $forbidden"
    }
}

$controllerText = Get-Content -LiteralPath $controller -Raw
foreach ($marker in @(
    'WidgetFixedPreset::GlassClock',
    'WidgetFixedPreset::TodayTasks',
    'WidgetFixedPreset::WeatherGlass',
    'PlacementFree',
    'IntersectsWithGap',
    'AutomaticPlacement',
    'CreatePreset',
    'MoveTo',
    'service_.CreateNativeWidget')) {
    if (-not $controllerText.Contains($marker)) {
        throw "Fixed-format Widget implementation missing marker: $marker"
    }
}
foreach ($forbidden in @('SetSize(', 'MoveToMonitor(', 'SetParent(', 'FindWindowW(', 'FindWindowExW(', 'WorkerW', 'Progman', '0x052C')) {
    if ($controllerText.Contains($forbidden)) {
        throw "Widget product controller regained deferred editor/shell ownership: $forbidden"
    }
}

$surfaceText = Get-Content -LiteralPath $surfaceChild -Raw
foreach ($marker in @(
    'CreateWidgetDragHandle',
    'BeginWidgetDrag',
    'EndWidgetDrag',
    'WidgetService',
    'WidgetUpdateRequest',
    'RaiseWidgetDragHandle')) {
    if (-not $surfaceText.Contains($marker)) {
        throw "Widget desktop drag surface missing marker: $marker"
    }
}
if ($surfaceText.Contains('WS_EX_TRANSPARENT') -and $surfaceText -match 'CreateWidgetDragHandle[\s\S]{0,400}WS_EX_TRANSPARENT') {
    throw 'Widget drag handle must remain hit-testable; WS_EX_TRANSPARENT breaks desktop dragging.'
}

$acceptanceText = Get-Content -LiteralPath $acceptance -Raw
foreach ($marker in @(
    'FixedShowcaseReady',
    'kM3Showcase',
    'enabledShowcase.size() != kM3Showcase.size()',
    'preset-owned geometry',
    'Overlaps')) {
    if (-not $acceptanceText.Contains($marker)) {
        throw "M3 acceptance no longer proves the full fixed showcase set: $marker"
    }
}

$configText = Get-Content -LiteralPath $configContinuity -Raw
foreach ($marker in @(
    'turingdesk.widget-acceptance-config.v2',
    'AppendSized(out, widget.title)',
    'id/title/monitorId/')) {
    if (-not $configText.Contains($marker)) {
        throw "M3 acceptance no longer freezes fixed showcase identity across phases: $marker"
    }
}

$docText = Get-Content -LiteralPath $doc -Raw
foreach ($marker in @(
    'Fixed showcase formats',
    'Desktop drag behavior',
    'drag widgets on the desktop',
    'click create three times',
    'real ARM64 Windows visible-runtime acceptance')) {
    if (-not $docText.Contains($marker)) {
        throw "Widget product documentation missing fixed-format + drag contract marker: $marker"
    }
}

Write-Host 'Widget product model OK: M3 stays fixed-format, collision-safe on create, supports desktop drag persistence, requires the full three-widget acceptance set, freezes showcase identity across phases, and remains free of resize/monitor-edit APIs while DesktopShellHost is the only desktop attachment owner.'
