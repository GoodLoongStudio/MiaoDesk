param()

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

$probe = Join-Path $root 'src/native/src/desktop/widgets/WidgetRuntimeAcceptance.cpp'
$main = Join-Path $root 'src/native/src/desktop/widgets/WidgetRuntimeAcceptanceMain.cpp'
$header = Join-Path $root 'src/native/include/turingdesk/WidgetRuntimeAcceptance.h'
$runner = Join-Path $root 'scripts/run-widget-runtime-acceptance.ps1'
$cmake = Join-Path $root 'src/native/CMakeLists.txt'
$doc = Join-Path $root 'docs/WIDGET_RUNTIME_HEALTH_M3.md'
$sequenceDoc = Join-Path $root 'docs/WIDGET_ACCEPTANCE_SEQUENCE_M3.md'

foreach ($path in @($probe, $main, $header, $runner, $cmake, $doc, $sequenceDoc)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing M3 Widget acceptance contract input: $path" }
}

$headerText = Get-Content -LiteralPath $header -Raw
foreach ($marker in @('Passed = 0', 'InteractiveDesktopUnavailable = 60', 'NoEnabledWebWidget = 61', 'RuntimeHealthUnavailable = 62', 'SurfaceUnhealthy = 63', 'ReportWriteFailed = 64', 'BaselineMissing = 65', 'BaselineMismatch = 66', 'SequenceOutOfOrder = 67')) {
    if (-not $headerText.Contains($marker)) { throw "Widget acceptance exit-code contract missing marker: $marker" }
}

$probeText = Get-Content -LiteralPath $probe -Raw
foreach ($marker in @('WidgetService', 'GetRuntimeHealth', 'InteractiveDesktopAvailable', 'OpenInputDesktop', 'renderingHealthy', 'widget-acceptance-', 'widget-acceptance-baseline.ids', 'widget-acceptance-sequence.phase', 'CheckPhaseContinuity', 'baselineStatus', 'sequenceStatus', 'ExpectedPreviousPhase', 'WriteSequencePhase', 'SurfaceIds')) {
    if (-not $probeText.Contains($marker)) { throw "Widget acceptance probe missing marker: $marker" }
}
foreach ($forbidden in @('FindWindowW(', 'FindWindowExW(', 'EnumWindows(', 'SetParent(', 'SetWindowPos(', 'GetPrivateProfileStringW')) {
    if ($probeText.Contains($forbidden)) { throw "Widget acceptance probe bypasses Widget/DesktopShell domain ownership: $forbidden" }
}

$mainText = Get-Content -LiteralPath $main -Raw
if (-not $mainText.Contains('RunWidgetRuntimeAcceptanceProbe') -or -not $mainText.Contains('--phase=')) {
    throw 'Widget acceptance executable must delegate to the Widget-domain probe and preserve phase labels.'
}

$runnerText = Get-Content -LiteralPath $runner -Raw
foreach ($marker in @(
    "ValidateSet('baseline','settings','search','explorer','monitor')",
    'Get-CurrentSessionExplorerPids',
    'widget-acceptance-search.explorer-pids',
    "if (`$Phase -eq 'explorer')",
    'Explorer recovery is unproven',
    "if (`$Phase -eq 'search')",
    'Get-CurrentDisplayTopology',
    'System.Windows.Forms.Screen',
    'widget-acceptance-explorer.monitor-topology',
    'widget-acceptance-monitor.topology-transition',
    'Write-MonitorTransitionEvidence',
    'Wait-ForMonitorTopologyTransition',
    "if (`$Phase -eq 'monitor')",
    'Monitor recovery is unproven',
    "60 { 'interactive Windows desktop unavailable' }",
    "63 { 'one or more Widget surfaces are unhealthy' }",
    "65 { 'baseline identity set missing; run the baseline phase first' }",
    "66 { 'enabled Widget identity set changed since baseline' }",
    "67 { 'acceptance phase is out of order; run baseline -> settings -> search -> explorer -> monitor without skipping a successful phase' }")) {
    if (-not $runnerText.Contains($marker)) { throw "Widget acceptance runner missing stable phase/evidence mapping: $marker" }
}
foreach ($forbidden in @('FindWindowW(', 'FindWindowExW(', 'SetParent(', 'SetWindowPos(', 'Progman', 'WorkerW', 'SHELLDLL_DefView')) {
    if ($runnerText.Contains($forbidden)) { throw "Widget acceptance runner must not regain shell HWND ownership: $forbidden" }
}

$cmakeText = Get-Content -LiteralPath $cmake -Raw
foreach ($marker in @('TuringDeskWidgetAcceptance', 'WidgetRuntimeAcceptance.cpp', 'WidgetRuntimeAcceptanceMain.cpp', 'TuringDeskWidgetAcceptanceContractCheck')) {
    if (-not $cmakeText.Contains($marker)) { throw "M3 acceptance probe missing from build graph: $marker" }
}

$docText = Get-Content -LiteralPath $doc -Raw
foreach ($marker in @('TuringDeskWidgetAcceptance.exe', 'baseline', 'settings', 'search', 'explorer', 'monitor', 'non-interactive CI', 'sequence cursor')) {
    if (-not $docText.Contains($marker)) { throw "M3 acceptance documentation missing marker: $marker" }
}

$sequenceText = Get-Content -LiteralPath $sequenceDoc -Raw
foreach ($marker in @('identity set', 'baselineStatus', 'sequenceStatus', 'sequence cursor', 'SequenceOutOfOrder', 'BaselineMissing', 'BaselineMismatch', 'PID/HWND', 'explorer.exe PID', 'Explorer restart evidence', 'display topology', 'monitor recovery evidence', 'widget-acceptance-monitor.topology-transition')) {
    if (-not $sequenceText.Contains($marker)) { throw "M3 acceptance sequence documentation missing marker: $marker" }
}

Write-Host 'M3 Widget acceptance contract OK: real-Windows probe consumes WidgetService health, rejects non-interactive sessions, preserves Widget identity continuity, enforces ordered phase evidence, requires observed Explorer restart and durable display-topology transition evidence, and does not regain HWND/shell ownership.'
