param()

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

$probe = Join-Path $root 'src/native/src/desktop/widgets/WidgetRuntimeAcceptance.cpp'
$configContinuity = Join-Path $root 'src/native/src/desktop/widgets/WidgetAcceptanceConfigContinuity.cpp'
$main = Join-Path $root 'src/native/src/desktop/widgets/WidgetRuntimeAcceptanceMain.cpp'
$header = Join-Path $root 'src/native/include/turingdesk/WidgetRuntimeAcceptance.h'
$runner = Join-Path $root 'scripts/run-widget-runtime-acceptance.ps1'
$confirmer = Join-Path $root 'scripts/confirm-widget-visual-acceptance.ps1'
$sealer = Join-Path $root 'scripts/seal-widget-acceptance-evidence.ps1'
$verifier = Join-Path $root 'scripts/verify-widget-acceptance-evidence.ps1'
$sessionVerifier = Join-Path $root 'scripts/verify-widget-acceptance-session-evidence.ps1'
$performancePolicy = Join-Path $root 'src/native/src/desktop/performance/WallpaperPerformancePolicy.cpp'
$cmake = Join-Path $root 'src/native/CMakeLists.txt'
$doc = Join-Path $root 'docs/WIDGET_RUNTIME_HEALTH_M3.md'
$sequenceDoc = Join-Path $root 'docs/WIDGET_ACCEPTANCE_SEQUENCE_M3.md'
$evidenceDoc = Join-Path $root 'docs/WIDGET_ACCEPTANCE_EVIDENCE_M3.md'
$placementDoc = Join-Path $root 'docs/WIDGET_PLACEMENT_HEALTH_M3.md'

foreach ($path in @($probe, $configContinuity, $main, $header, $runner, $confirmer, $sealer, $verifier, $sessionVerifier, $performancePolicy, $cmake, $doc, $sequenceDoc, $evidenceDoc, $placementDoc)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing M3 Widget acceptance contract input: $path" }
}

$headerText = Get-Content -LiteralPath $header -Raw
foreach ($marker in @('Passed = 0','InteractiveDesktopUnavailable = 60','NoEnabledWebWidget = 61','RuntimeHealthUnavailable = 62','SurfaceUnhealthy = 63','ReportWriteFailed = 64','BaselineMissing = 65','BaselineMismatch = 66','SequenceOutOfOrder = 67','CheckWidgetAcceptanceConfigContinuity')) {
    if (-not $headerText.Contains($marker)) { throw "Widget acceptance exit-code/config contract missing marker: $marker" }
}

$probeText = Get-Content -LiteralPath $probe -Raw
foreach ($marker in @('WidgetService','GetRuntimeHealth','InteractiveDesktopAvailable','OpenInputDesktop','renderingHealthy','widget-acceptance-','widget-acceptance-baseline.ids','widget-acceptance-baseline.session','BaselineSessionPath','ProcessIdToSessionId','CurrentSessionId','sessionStatus','sessionId=','widget-acceptance-sequence.phase','CheckPhaseContinuity','baselineStatus','sequenceStatus','ExpectedPreviousPhase','WriteSequencePhase','SurfaceIds','monitorId=','monitorReported=','monitorValid=','geometryReported=','geometryValid=','expectedRect=','actualRect=')) {
    if (-not $probeText.Contains($marker)) { throw "Widget acceptance probe missing marker: $marker" }
}
foreach ($forbidden in @('FindWindowW(','FindWindowExW(','EnumWindows(','SetParent(','SetWindowPos(','GetPrivateProfileStringW')) {
    if ($probeText.Contains($forbidden)) { throw "Widget acceptance probe bypasses Widget/DesktopShell domain ownership: $forbidden" }
}

$configText = Get-Content -LiteralPath $configContinuity -Raw
foreach ($marker in @('WidgetService','service.List(&widgets)','widget-acceptance-baseline.config','turingdesk.widget-acceptance-config.v2','widget.title','widget.monitorId','widget.x','widget.y','widget.width','widget.height','widget.zIndex','widget.enabled','widget.kind','std::bit_cast','BaselineMismatch')) {
    if (-not $configText.Contains($marker)) { throw "Widget acceptance config continuity missing service-routed showcase marker: $marker" }
}
foreach ($forbidden in @('DesktopWidgetStore store','GetPrivateProfile','FindWindowW(','FindWindowExW(','EnumWindows(','SetParent(','SetWindowPos(','Progman','WorkerW','SHELLDLL_DefView')) {
    if ($configText.Contains($forbidden)) { throw "Widget acceptance config continuity regained private store/shell ownership: $forbidden" }
}

$mainText = Get-Content -LiteralPath $main -Raw
foreach ($marker in @(
    'RunWidgetRuntimeAcceptanceProbe',
    '--phase=',
    'CheckWidgetAcceptanceConfigContinuity',
    'if (!baseline)',
    'if (baseline)',
    'FixedShowcaseReady',
    'kM3Showcase',
    'PhaseContextReady',
    'ProcessImageMatches',
    'EnumWindows(',
    'TuringDesk.Native.DesktopLibrary',
    'TuringDeskWallpaper.exe',
    'TuringDesk.Native.SearchWindow',
    'TuringDesk.exe',
    'StructuredLifecycleReady',
    'service.GetRuntimeHealth(&health)',
    'environmentReported',
    'controllerReported',
    'navigationReported',
    'environmentReady',
    'controllerReady',
    'navigationReady')) {
    if (-not $mainText.Contains($marker)) { throw "Widget acceptance executable missing strict showcase/runtime/config/context/lifecycle routing: $marker" }
}
foreach ($forbidden in @('SetParent(','SetWindowPos(','SendMessageTimeoutW(','0x052C','Progman','WorkerW','SHELLDLL_DefView','DesktopWidgetStore store','GetPrivateProfileStringW')) {
    if ($mainText.Contains($forbidden)) { throw "Widget acceptance executable regained Widget store or desktop attachment ownership: $forbidden" }
}
$runtimeProbeCall = 'const auto code = turingdesk::desktop::RunWidgetRuntimeAcceptanceProbe('
$firstShowcaseCheck = $mainText.LastIndexOf('FixedShowcaseReady(&failure)')
$firstConfigCheck = $mainText.IndexOf('CheckWidgetAcceptanceConfigContinuity')
$runtimeProbe = $mainText.IndexOf($runtimeProbeCall)
if ($runtimeProbe -lt 0) {
    throw 'Widget acceptance executable is missing the concrete runtime probe call used for phase advancement.'
}
$phaseContextCheck = $mainText.LastIndexOf('PhaseContextReady(phase, &failure)', $runtimeProbe)
$lifecycleCheck = $mainText.LastIndexOf('StructuredLifecycleReady(&failure)', $runtimeProbe)
if ($firstShowcaseCheck -lt 0 -or $firstShowcaseCheck -gt $runtimeProbe) {
    throw 'Fixed three-clock showcase validation must run before the runtime probe can advance the phase sequence.'
}
if ($firstConfigCheck -lt 0 -or $firstConfigCheck -gt $runtimeProbe) {
    throw 'Non-baseline Widget config continuity must be checked before the runtime probe can advance the phase sequence.'
}
if ($phaseContextCheck -lt 0 -or $phaseContextCheck -gt $runtimeProbe) {
    throw 'Settings/Search product process/window context must be checked before the runtime probe can advance the phase sequence.'
}
if ($lifecycleCheck -lt 0 -or $lifecycleCheck -gt $runtimeProbe) {
    throw 'Strict reported-and-ready WebView2 lifecycle must be checked before the runtime probe can advance the phase sequence.'
}
$afterRuntime = $mainText.Substring($runtimeProbe + $runtimeProbeCall.Length)
if ($afterRuntime.Contains('PhaseContextReady(phase, &failure)') -or $afterRuntime.Contains('StructuredLifecycleReady(&failure)') -or $afterRuntime.Contains('FixedShowcaseReady(&failure)')) {
    throw 'Fallible showcase/product-context/lifecycle gates must not run after the runtime probe has had a chance to advance the phase cursor.'
}

$runnerText = Get-Content -LiteralPath $runner -Raw
foreach ($marker in @("ValidateSet('baseline','settings','search','explorer','monitor')",'Get-CurrentSessionExplorerPids','widget-acceptance-search.explorer-pids','Explorer recovery is unproven','Get-CurrentDisplayTopology','widget-acceptance-explorer.monitor-topology','widget-acceptance-monitor.topology-transition','Wait-ForMonitorTopologyTransition','Monitor recovery is unproven','Capture-DesktopVisualEvidence','CopyFromScreen','widget-acceptance-binary.sha256','Assert-AcceptanceBinaryContinuity','Get-FileHash','virtualBounds=','Initialize-ForegroundWindowInterop','GetForegroundWindow','Wait-ForExpectedTuringDeskWindowEvidence','TuringDesk.Native.DesktopLibrary','TuringDesk.Native.SearchWindow','turingdesk.widget-window-evidence.v1','widget-acceptance-$AcceptancePhase.window.json','widget-acceptance-baseline.config','$configCheckpoint')) {
    if (-not $runnerText.Contains($marker)) { throw "Widget acceptance runner missing stable phase/evidence/binary/config/window mapping: $marker" }
}

$confirmerText = Get-Content -LiteralPath $confirmer -Raw
foreach ($marker in @('turingdesk.widget-visual-acceptance.v1','Reviewer','WallpaperBelowWidget','IconsAboveWidget','DesktopIconsUsable','SettingsKeepsWidgetVisible','SearchKeepsWidgetVisible','ExplorerRecoveryVisible','MonitorRecoveryVisible','widget-acceptance-human-visual.json','widget-acceptance-binary.sha256','screenshotSha256','Get-FileHash')) {
    if (-not $confirmerText.Contains($marker)) { throw "Widget human visual acceptance confirmer missing marker: $marker" }
}

$sealerText = Get-Content -LiteralPath $sealer -Raw
foreach ($marker in @("schema = 'turingdesk.widget-acceptance-evidence.v1'",'widget-acceptance-baseline.session','widget-acceptance-baseline.config','placementConfig',"fileName = 'widget-acceptance-baseline.config'",'Placement config SHA-256','Read-BaselineSessionId','baselineSessionId','widget-acceptance-human-visual.json','widget-acceptance-human-visual.json.sha256','Assert-HumanVisualAttestation','humanVisualAcceptance','turingdesk.widget-visual-acceptance.v1','widget-acceptance-binary.sha256','widget-acceptance-settings.window.json','widget-acceptance-search.window.json','observedProductWindows','widget-acceptance-evidence.manifest.json','widget-acceptance-evidence.manifest.sha256')) {
    if (-not $sealerText.Contains($marker)) { throw "Widget acceptance evidence sealer missing placement/evidence marker: $marker" }
}

$verifierText = Get-Content -LiteralPath $verifier -Raw
foreach ($marker in @('placementConfig','widget-acceptance-baseline.config','placement configuration SHA-256 no longer matches','placement configuration length no longer matches','humanVisualAcceptance','widget-acceptance-human-visual.json','widget-acceptance-human-visual.json.sha256','turingdesk.widget-visual-acceptance.v1','wallpaperBelowWidget','iconsAboveWidget','desktopIconsUsable','settingsKeepsWidgetVisible','searchKeepsWidgetVisible','explorerRecoveryVisible','monitorRecoveryVisible','screenshotSha256','Assert-ObservedProductWindow','turingdesk.widget-window-evidence.v1','TuringDesk.Native.DesktopLibrary','TuringDesk.Native.SearchWindow','widget-acceptance-settings.window.json','widget-acceptance-search.window.json','observedProductWindows','widget-acceptance-baseline.session','baselineSessionId','verify-widget-acceptance-session-evidence.ps1','Settings product-window evidence was not observed after the baseline screenshot','Search product-window evidence was not observed after the Settings phase screenshot')) {
    if (-not $verifierText.Contains($marker)) { throw "Widget acceptance evidence verifier missing placement/visual/window/session attestation marker: $marker" }
}

$sessionVerifierText = Get-Content -LiteralPath $sessionVerifier -Raw
foreach ($marker in @('widget-acceptance-baseline.session','baselineStatus','sessionStatus','sessionId','enabledWebCount','runtimeReported','runtimeHealthy','phaseOrder','Count-WidgetTruth','monitorValid','geometryValid','visible','zOrderValid','renderingHealthy')) {
    if (-not $sessionVerifierText.Contains($marker)) { throw "Widget acceptance session verifier missing health/session/placement marker: $marker" }
}
foreach ($semanticCheck in @(
    "`$phaseOrder = @('baseline','settings','search','explorer','monitor')",
    "[int]`$report['sessionId'] -ne `$baselineSessionId",
    '[string]$report[''sessionStatus''] -ne $expectedSessionStatus',
    '[int]$attestation.sessionId -ne $baselineSessionId',
    "foreach (`$field in @('monitorValid','geometryValid','visible','zOrderValid','renderingHealthy'))",
    'if ($healthyCount -ne $widgetCount)'
)) {
    if (-not $sessionVerifierText.Contains($semanticCheck)) {
        throw "Widget acceptance session verifier is missing executable phase/session/placement validation: $semanticCheck"
    }
}

$performanceText = Get-Content -LiteralPath $performancePolicy -Raw
foreach ($marker in @('IsTuringDeskWindowClass','TuringDesk.Native.','IsIgnoredForeground','foreground == settingsWindow','IsTuringDeskWindowClass(className)','TuringDesk.Native.DesktopLibrary')) {
    if (-not $performanceText.Contains($marker)) {
        throw "M3 Settings/Search foreground must remain excluded from wallpaper performance pause detection: $marker"
    }
}

foreach ($text in @($runnerText, $confirmerText, $sealerText, $verifierText, $sessionVerifierText, $configText)) {
    foreach ($forbidden in @('FindWindowW(','FindWindowExW(','EnumWindows(','SetParent(','SetWindowPos(','Progman','WorkerW','SHELLDLL_DefView')) {
        if ($text.Contains($forbidden)) { throw "M3 acceptance diagnostics must not regain shell HWND ownership: $forbidden" }
    }
}

$cmakeText = Get-Content -LiteralPath $cmake -Raw
foreach ($marker in @('TuringDeskWidgetAcceptance','WidgetRuntimeAcceptance.cpp','WidgetRuntimeAcceptanceMain.cpp','WidgetAcceptanceConfigContinuity.cpp','TuringDeskWidgetAcceptanceContractCheck','src/desktop/wallpaper/monitor/WallpaperMonitorLayout.cpp')) {
    if (-not $cmakeText.Contains($marker)) { throw "M3 acceptance probe missing from build graph: $marker" }
}

$docText = Get-Content -LiteralPath $doc -Raw
foreach ($marker in @('TuringDeskWidgetAcceptance.exe','baseline','settings','search','explorer','monitor','non-interactive CI','sequence cursor')) {
    if (-not $docText.Contains($marker)) { throw "M3 acceptance documentation missing marker: $marker" }
}
$sequenceText = Get-Content -LiteralPath $sequenceDoc -Raw
foreach ($marker in @('identity set','placement configuration','widget-acceptance-baseline.config','WidgetService::List','Windows session','baselineStatus','sessionStatus','sessionId','sequenceStatus','sequence cursor','SequenceOutOfOrder','BaselineMissing','BaselineMismatch','PID/HWND','explorer.exe PID','Explorer restart evidence','display topology','monitor recovery evidence','visual evidence','.png.sha256','virtual desktop','TuringDesk.Native.DesktopLibrary','TuringDesk.Native.SearchWindow','observed product window')) {
    if (-not $sequenceText.Contains($marker)) { throw "M3 acceptance sequence documentation missing marker: $marker" }
}
$evidenceText = Get-Content -LiteralPath $evidenceDoc -Raw
foreach ($marker in @('seal-widget-acceptance-evidence.ps1','confirm-widget-visual-acceptance.ps1','verify-widget-acceptance-session-evidence.ps1','turingdesk.widget-acceptance-evidence.v1','turingdesk.widget-visual-acceptance.v1','widget-acceptance-baseline.session','widget-acceptance-baseline.config','placementConfig','stable placement configuration','widget-acceptance-human-visual.json','real ARM64 Windows','Human review','chronology','acceptance binary','same Windows session','healthy phase reports','widget-acceptance-settings.window.json','widget-acceptance-search.window.json','observed Settings/Search')) {
    if (-not $evidenceText.Contains($marker)) { throw "M3 acceptance evidence documentation missing marker: $marker" }
}
$placementText = Get-Content -LiteralPath $placementDoc -Raw
foreach ($marker in @('monitorReported','monitorValid','geometryReported','geometryValid','expectedLeft','actualLeft','geometry_mismatch','monitor reconnect acceptance','real-Windows visual gate','Independent acceptance verification','zOrderValid=true')) {
    if (-not $placementText.Contains($marker)) { throw "M3 placement health documentation missing marker: $marker" }
}

Write-Host 'M3 Widget acceptance contract OK: runtime health includes target-monitor geometry recovery; the full fixed three-clock showcase and v2 identity/placement configuration are checked before phase advancement; Settings/Search acceptance is pinned to the expected visible product class/process in the same session; real acceptance rejects legacy/unreported WebView2 lifecycle; stable Widget showcase configuration is service-routed and sealed by hash/length; ordered recovery evidence, binary continuity, hashed screenshots and explicit human visual attestation remain required without regaining store/HWND/shell ownership.'