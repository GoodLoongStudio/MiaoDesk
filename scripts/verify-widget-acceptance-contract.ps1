param()

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

$probe = Join-Path $root 'src/native/src/desktop/widgets/WidgetRuntimeAcceptance.cpp'
$main = Join-Path $root 'src/native/src/desktop/widgets/WidgetRuntimeAcceptanceMain.cpp'
$header = Join-Path $root 'src/native/include/turingdesk/WidgetRuntimeAcceptance.h'
$runner = Join-Path $root 'scripts/run-widget-runtime-acceptance.ps1'
$confirmer = Join-Path $root 'scripts/confirm-widget-visual-acceptance.ps1'
$sealer = Join-Path $root 'scripts/seal-widget-acceptance-evidence.ps1'
$verifier = Join-Path $root 'scripts/verify-widget-acceptance-evidence.ps1'
$cmake = Join-Path $root 'src/native/CMakeLists.txt'
$doc = Join-Path $root 'docs/WIDGET_RUNTIME_HEALTH_M3.md'
$sequenceDoc = Join-Path $root 'docs/WIDGET_ACCEPTANCE_SEQUENCE_M3.md'
$evidenceDoc = Join-Path $root 'docs/WIDGET_ACCEPTANCE_EVIDENCE_M3.md'
$placementDoc = Join-Path $root 'docs/WIDGET_PLACEMENT_HEALTH_M3.md'

foreach ($path in @($probe, $main, $header, $runner, $confirmer, $sealer, $verifier, $cmake, $doc, $sequenceDoc, $evidenceDoc, $placementDoc)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing M3 Widget acceptance contract input: $path" }
}

$headerText = Get-Content -LiteralPath $header -Raw
foreach ($marker in @('Passed = 0','InteractiveDesktopUnavailable = 60','NoEnabledWebWidget = 61','RuntimeHealthUnavailable = 62','SurfaceUnhealthy = 63','ReportWriteFailed = 64','BaselineMissing = 65','BaselineMismatch = 66','SequenceOutOfOrder = 67')) {
    if (-not $headerText.Contains($marker)) { throw "Widget acceptance exit-code contract missing marker: $marker" }
}

$probeText = Get-Content -LiteralPath $probe -Raw
foreach ($marker in @('WidgetService','GetRuntimeHealth','InteractiveDesktopAvailable','OpenInputDesktop','renderingHealthy','widget-acceptance-','widget-acceptance-baseline.ids','widget-acceptance-baseline.session','BaselineSessionPath','ProcessIdToSessionId','CurrentSessionId','sessionStatus','sessionId=','widget-acceptance-sequence.phase','CheckPhaseContinuity','baselineStatus','sequenceStatus','ExpectedPreviousPhase','WriteSequencePhase','SurfaceIds','monitorId=','monitorReported=','monitorValid=','geometryReported=','geometryValid=','expectedRect=','actualRect=')) {
    if (-not $probeText.Contains($marker)) { throw "Widget acceptance probe missing marker: $marker" }
}
foreach ($forbidden in @('FindWindowW(','FindWindowExW(','EnumWindows(','SetParent(','SetWindowPos(','GetPrivateProfileStringW')) {
    if ($probeText.Contains($forbidden)) { throw "Widget acceptance probe bypasses Widget/DesktopShell domain ownership: $forbidden" }
}

$mainText = Get-Content -LiteralPath $main -Raw
if (-not $mainText.Contains('RunWidgetRuntimeAcceptanceProbe') -or -not $mainText.Contains('--phase=')) { throw 'Widget acceptance executable must delegate to the Widget-domain probe and preserve phase labels.' }

$runnerText = Get-Content -LiteralPath $runner -Raw
foreach ($marker in @("ValidateSet('baseline','settings','search','explorer','monitor')",'Get-CurrentSessionExplorerPids','widget-acceptance-search.explorer-pids','Explorer recovery is unproven','Get-CurrentDisplayTopology','widget-acceptance-explorer.monitor-topology','widget-acceptance-monitor.topology-transition','Wait-ForMonitorTopologyTransition','Monitor recovery is unproven','Capture-DesktopVisualEvidence','CopyFromScreen','widget-acceptance-binary.sha256','Assert-AcceptanceBinaryContinuity','Get-FileHash','virtualBounds=')) {
    if (-not $runnerText.Contains($marker)) { throw "Widget acceptance runner missing stable phase/evidence/binary mapping: $marker" }
}

$confirmerText = Get-Content -LiteralPath $confirmer -Raw
foreach ($marker in @('turingdesk.widget-visual-acceptance.v1','Reviewer','WallpaperBelowWidget','IconsAboveWidget','DesktopIconsUsable','SettingsKeepsWidgetVisible','SearchKeepsWidgetVisible','ExplorerRecoveryVisible','MonitorRecoveryVisible','widget-acceptance-human-visual.json','widget-acceptance-binary.sha256','screenshotSha256','Get-FileHash')) {
    if (-not $confirmerText.Contains($marker)) { throw "Widget human visual acceptance confirmer missing marker: $marker" }
}

$sealerText = Get-Content -LiteralPath $sealer -Raw
foreach ($marker in @("schema = 'turingdesk.widget-acceptance-evidence.v1'",'widget-acceptance-human-visual.json','widget-acceptance-human-visual.json.sha256','Assert-HumanVisualAttestation','humanVisualAcceptance','turingdesk.widget-visual-acceptance.v1','widget-acceptance-binary.sha256','widget-acceptance-evidence.manifest.json','widget-acceptance-evidence.manifest.sha256')) {
    if (-not $sealerText.Contains($marker)) { throw "Widget acceptance evidence sealer missing marker: $marker" }
}

$verifierText = Get-Content -LiteralPath $verifier -Raw
foreach ($marker in @('humanVisualAcceptance','widget-acceptance-human-visual.json','widget-acceptance-human-visual.json.sha256','turingdesk.widget-visual-acceptance.v1','wallpaperBelowWidget','iconsAboveWidget','desktopIconsUsable','settingsKeepsWidgetVisible','searchKeepsWidgetVisible','explorerRecoveryVisible','monitorRecoveryVisible','screenshotSha256')) {
    if (-not $verifierText.Contains($marker)) { throw "Widget acceptance evidence verifier missing visual attestation marker: $marker" }
}

foreach ($text in @($runnerText, $confirmerText, $sealerText, $verifierText)) {
    foreach ($forbidden in @('FindWindowW(','FindWindowExW(','EnumWindows(','SetParent(','SetWindowPos(','Progman','WorkerW','SHELLDLL_DefView')) {
        if ($text.Contains($forbidden)) { throw "M3 acceptance diagnostics must not regain shell HWND ownership: $forbidden" }
    }
}

$cmakeText = Get-Content -LiteralPath $cmake -Raw
foreach ($marker in @('TuringDeskWidgetAcceptance','WidgetRuntimeAcceptance.cpp','WidgetRuntimeAcceptanceMain.cpp','TuringDeskWidgetAcceptanceContractCheck','src/desktop/wallpaper/monitor/WallpaperMonitorLayout.cpp')) {
    if (-not $cmakeText.Contains($marker)) { throw "M3 acceptance probe missing from build graph: $marker" }
}

$docText = Get-Content -LiteralPath $doc -Raw
foreach ($marker in @('TuringDeskWidgetAcceptance.exe','baseline','settings','search','explorer','monitor','non-interactive CI','sequence cursor')) {
    if (-not $docText.Contains($marker)) { throw "M3 acceptance documentation missing marker: $marker" }
}
$sequenceText = Get-Content -LiteralPath $sequenceDoc -Raw
foreach ($marker in @('identity set','Windows session','baselineStatus','sessionStatus','sessionId','sequenceStatus','sequence cursor','SequenceOutOfOrder','BaselineMissing','BaselineMismatch','PID/HWND','explorer.exe PID','Explorer restart evidence','display topology','monitor recovery evidence','visual evidence','.png.sha256','virtual desktop')) {
    if (-not $sequenceText.Contains($marker)) { throw "M3 acceptance sequence documentation missing marker: $marker" }
}
$evidenceText = Get-Content -LiteralPath $evidenceDoc -Raw
foreach ($marker in @('seal-widget-acceptance-evidence.ps1','confirm-widget-visual-acceptance.ps1','turingdesk.widget-acceptance-evidence.v1','turingdesk.widget-visual-acceptance.v1','widget-acceptance-human-visual.json','real ARM64 Windows','Human review','chronology','acceptance binary')) {
    if (-not $evidenceText.Contains($marker)) { throw "M3 acceptance evidence documentation missing marker: $marker" }
}
$placementText = Get-Content -LiteralPath $placementDoc -Raw
foreach ($marker in @('monitorReported','monitorValid','geometryReported','geometryValid','expectedLeft','actualLeft','geometry_mismatch','monitor reconnect acceptance','real-Windows visual gate')) {
    if (-not $placementText.Contains($marker)) { throw "M3 placement health documentation missing marker: $marker" }
}

Write-Host 'M3 Widget acceptance contract OK: runtime health includes target-monitor geometry recovery, same-session ordered recovery evidence, binary continuity, hashed screenshots and explicit human visual attestation before sealing, without regaining HWND/shell ownership.'