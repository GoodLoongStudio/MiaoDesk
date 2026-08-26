$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$runnerPath = Join-Path $root 'scripts\run-installed-widget-acceptance.ps1'
$phaseRunnerPath = Join-Path $root 'scripts\run-widget-runtime-acceptance.ps1'
$workflowPath = Join-Path $root '.github\workflows\native-search-windows.yml'
$docPath = Join-Path $root 'docs\WIDGET_INSTALLED_ACCEPTANCE_M3.md'

foreach ($path in @($runnerPath, $phaseRunnerPath, $workflowPath, $docPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Installed M3 Widget acceptance contract input is missing: $path"
    }
}

$runner = Get-Content -LiteralPath $runnerPath -Raw
foreach ($marker in @(
    "ValidateSet('baseline','settings','search','explorer','monitor')",
    "'TuringDesk\NativeTest'",
    "'TuringDeskWidgetAcceptance.exe'",
    "'.installed-build-sha'",
    "run-widget-runtime-acceptance.ps1",
    '-BuildDir $installedDir',
    '-Phase $Phase'
)) {
    if (-not $runner.Contains($marker)) {
        throw "Installed M3 acceptance runner is missing marker: $marker"
    }
}

$workflow = Get-Content -LiteralPath $workflowPath -Raw
foreach ($marker in @(
    'Copy-Item build/src/native/Release/TuringDeskWidgetAcceptance.exe build/package/TuringDeskWidgetAcceptance.exe -Force',
    'name: TuringDesk-Native-Search-ARM64',
    'scripts/run-installed-widget-acceptance.ps1'
)) {
    if (-not $workflow.Contains($marker)) {
        throw "ARM64 compact package/trigger is missing installed M3 acceptance marker: $marker"
    }
}

$doc = Get-Content -LiteralPath $docPath -Raw
foreach ($marker in @(
    'TuringDeskWidgetAcceptance.exe',
    'run-installed-widget-acceptance.ps1',
    'turingdesk.widget-acceptance-config.v2',
    'baseline -> settings -> search -> explorer -> monitor',
    'real ARM64 Windows',
    'Only after those checks pass may the M2/M3 real-Windows acceptance gates be closed and work advance to M4.'
)) {
    if (-not $doc.Contains($marker)) {
        throw "Installed M3 acceptance documentation is missing marker: $marker"
    }
}

$phaseRunner = Get-Content -LiteralPath $phaseRunnerPath -Raw
if (-not $phaseRunner.Contains('[string]$BuildDir')) {
    throw 'The five-phase M3 runner must continue accepting an explicit BuildDir so the installed package can be probed.'
}

foreach ($forbidden in @('SetParent(', 'SetWindowPos(', 'Progman', 'WorkerW', 'SHELLDLL_DefView')) {
    if ($runner.Contains($forbidden)) {
        throw "Installed M3 acceptance wrapper must not regain desktop-shell ownership: $forbidden"
    }
}

Write-Host 'Installed ARM64 M3 Widget acceptance contract verified.' -ForegroundColor Green
