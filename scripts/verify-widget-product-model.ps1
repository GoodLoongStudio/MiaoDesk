param()

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

$header = Join-Path $root 'src/native/include/turingdesk/DesktopWidgetController.h'
$controller = Join-Path $root 'src/native/src/desktop/widgets/DesktopWidgetController.cpp'
$doc = Join-Path $root 'docs/WIDGET_PRODUCT_MODEL_M3.md'

foreach ($path in @($header, $controller, $doc)) {
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
    '极简时钟',
    '日期时钟',
    '玻璃时钟',
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

$docText = Get-Content -LiteralPath $doc -Raw
foreach ($marker in @(
    'Current M3 simplification',
    '极简时钟',
    '日期时钟',
    '玻璃时钟',
    'Freeform editing is explicitly deferred',
    'real ARM64 Windows visible-runtime acceptance')) {
    if (-not $docText.Contains($marker)) {
        throw "Widget product documentation missing fixed-format contract marker: $marker"
    }
}

Write-Host 'Widget product model OK: M3 stays fixed-format, collision-safe and free of drag/resize/monitor-edit APIs while DesktopShellHost remains the only desktop attachment owner.'
