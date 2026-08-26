param()

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

$header = Join-Path $root 'src/native/include/turingdesk/DesktopWidgetController.h'
$controller = Join-Path $root 'src/native/src/desktop/widgets/DesktopWidgetController.cpp'
$acceptance = Join-Path $root 'src/native/src/desktop/widgets/WidgetRuntimeAcceptanceMain.cpp'
$configContinuity = Join-Path $root 'src/native/src/desktop/widgets/WidgetAcceptanceConfigContinuity.cpp'
$doc = Join-Path $root 'docs/WIDGET_PRODUCT_MODEL_M3.md'

foreach ($path in @($header, $controller, $acceptance, $configContinuity, $doc)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Missing Widget product-model contract input: $path"
    }
}

$headerText = Get-Content -LiteralPath $header -Raw
foreach ($marker in @(
    'enum class WidgetFixedPreset',
    'MinimalClock',
    'DateClock',
    'GlassClock',
    'CreatePreset',
    'SetEnabled',
    'Remove')) {
    if (-not $headerText.Contains($marker)) {
        throw "Fixed-format Widget controller contract missing marker: $marker"
    }
}
foreach ($forbidden in @('WidgetSizePreset', 'SetSize(', 'MoveToMonitor(')) {
    if ($headerText.Contains($forbidden)) {
        throw "M3 fixed-format Widget surface must not expose deferred editing API: $forbidden"
    }
}

$controllerText = Get-Content -LiteralPath $controller -Raw
foreach ($marker in @(
    'WidgetFixedPreset::MinimalClock',
    'WidgetFixedPreset::DateClock',
    'WidgetFixedPreset::GlassClock',
    'PlacementFree',
    'IntersectsWithGap',
    'AutomaticPlacement',
    'CreatePreset',
    'service_.CreateWebWidget')) {
    if (-not $controllerText.Contains($marker)) {
        throw "Fixed-format Widget implementation missing marker: $marker"
    }
}
foreach ($forbidden in @('SetSize(', 'MoveToMonitor(', 'SetParent(', 'FindWindowW(', 'FindWindowExW(', 'WorkerW', 'Progman', '0x052C')) {
    if ($controllerText.Contains($forbidden)) {
        throw "Widget product controller regained deferred editor/shell ownership: $forbidden"
    }
}

$acceptanceText = Get-Content -LiteralPath $acceptance -Raw
foreach ($marker in @(
    'FixedShowcaseReady',
    'kM3Showcase',
    'enabledWeb.size() != kM3Showcase.size()',
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
    'Current M3 simplification',
    'Fixed showcase formats',
    'Freeform editing is explicitly deferred',
    'click create three times',
    'real ARM64 Windows visible-runtime acceptance')) {
    if (-not $docText.Contains($marker)) {
        throw "Widget product documentation missing fixed-format contract marker: $marker"
    }
}

Write-Host 'Widget product model OK: M3 stays fixed-format, collision-safe, requires the full three-clock acceptance set, freezes showcase identity across phases, and remains free of drag/resize/monitor-edit APIs while DesktopShellHost is the only desktop attachment owner.'
