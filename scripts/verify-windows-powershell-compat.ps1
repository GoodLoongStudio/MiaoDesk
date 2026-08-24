# verify-windows-powershell-compat.ps1
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$scriptRoot = Join-Path $root 'scripts'

function Assert-AsciiFile([string]$Path) {
    if (-not (Test-Path $Path -PathType Leaf)) { throw "Missing file: $Path" }
    $bytes = [IO.File]::ReadAllBytes($Path)
    for ($i = 0; $i -lt $bytes.Length; $i++) {
        if ($bytes[$i] -gt 127) {
            throw "Windows PowerShell entrypoint must remain ASCII-only: $Path (byte offset $i)."
        }
    }
}

function Assert-PowerShellParses([string]$Path) {
    $tokens = $null
    $errors = $null
    [System.Management.Automation.Language.Parser]::ParseFile($Path, [ref]$tokens, [ref]$errors) | Out-Null
    if ($errors -and $errors.Count -gt 0) {
        $messages = ($errors | ForEach-Object { "line $($_.Extent.StartLineNumber): $($_.Message)" }) -join '; '
        throw "Windows PowerShell parser rejected $Path: $messages"
    }
}

$powerShellScripts = @(Get-ChildItem $scriptRoot -Filter '*.ps1' -File | Sort-Object Name)
if ($powerShellScripts.Count -eq 0) { throw 'No PowerShell scripts were found.' }
foreach ($file in $powerShellScripts) {
    Assert-AsciiFile $file.FullName
    Assert-PowerShellParses $file.FullName
}

$updateCmd = Join-Path $root 'UPDATE-TURINGDESK.cmd'
$deployCmd = Join-Path $root 'DEPLOY-NATIVE-ARM64.cmd'
$updateScript = Join-Path $scriptRoot 'update-turingdesk-arm64.ps1'
$deployScript = Join-Path $scriptRoot 'deploy-native-arm64.ps1'
$armWorkflow = Join-Path $root '.github\workflows\native-search-windows.yml'
$x64Workflow = Join-Path $root '.github\workflows\native-x64-validation.yml'
foreach ($cmd in @($updateCmd, $deployCmd)) { Assert-AsciiFile $cmd }

$updateText = [IO.File]::ReadAllText($updateCmd, [Text.Encoding]::ASCII)
$deployText = [IO.File]::ReadAllText($deployCmd, [Text.Encoding]::ASCII)
$updateScriptText = [IO.File]::ReadAllText($updateScript, [Text.Encoding]::ASCII)
$deployScriptText = [IO.File]::ReadAllText($deployScript, [Text.Encoding]::ASCII)
$armWorkflowText = [IO.File]::ReadAllText($armWorkflow)
$x64WorkflowText = [IO.File]::ReadAllText($x64Workflow)

foreach ($forbidden in @('^|', 'Codex-first', 'Codex CLI', 'Codex Relay')) {
    if ($updateText.Contains($forbidden) -or $deployText.Contains($forbidden)) {
        throw "One-click entrypoint contains a retired or fragile marker: $forbidden"
    }
}
foreach ($required in @(
    'update-turingdesk-arm64.ps1',
    'if not defined TD_UPDATER',
    'if not defined TD_UPDATE_URL',
    '$env:TD_UPDATE_URL',
    '$env:TD_UPDATER',
    '-File "%TD_UPDATER%"',
    'TD_BOOTSTRAP_SELF_TEST',
    '[scriptblock]::Create($t)'
)) {
    if (-not $updateText.Contains($required)) { throw "Updater bootstrap marker missing: $required" }
}
foreach ($required in @('scripts\deploy-native-arm64.ps1', 'git pull --ff-only')) {
    if (-not $deployText.Contains($required)) { throw "Deploy bootstrap marker missing: $required" }
}
foreach ($required in @(
    'NativeTest.previous-',
    'Rolling back TuringDesk installation',
    'Rollback completed.',
    'Move-Item -LiteralPath $DeployDir -Destination $previous',
    'NativeTest.update-state.json',
    'Recover-InterruptedUpdate',
    'Interrupted update recovery completed.',
    'TuringDeskArm64Updater',
    'WaitOne(0)',
    'Another TuringDesk update is already running.'
)) {
    if (-not $updateScriptText.Contains($required)) { throw "Updater safety marker missing: $required" }
}
foreach ($required in @(
    'verify-windows-powershell-compat.ps1',
    '$PowerShellGuard',
    'Verifying Windows PowerShell 5.1 entrypoints'
)) {
    if (-not $deployScriptText.Contains($required)) { throw "Local deploy compatibility preflight marker missing: $required" }
}
foreach ($workflowText in @($armWorkflowText, $x64WorkflowText)) {
    foreach ($required in @(
        'Exercise one-click updater bootstrap',
        'TD_BOOTSTRAP_SELF_TEST: 1',
        'TD_UPDATE_URL: https://raw.githubusercontent.com/GoodLoongStudio/TuringDesk/${{ github.sha }}/scripts/update-turingdesk-arm64.ps1',
        'UPDATE-TURINGDESK.cmd',
        'Verify checkout matches workflow commit',
        'git rev-parse HEAD',
        '$env:GITHUB_SHA'
    )) {
        if (-not $workflowText.Contains($required)) { throw "Windows exact-SHA workflow marker missing: $required" }
    }
}
foreach ($forbidden in @('workflow_run:', 'ref: main')) {
    if ($armWorkflowText.Contains($forbidden)) { throw "ARM64 workflow may drift away from the triggering commit: $forbidden" }
}

Write-Host 'Windows PowerShell 5.1 compatibility OK: entrypoints are ASCII-only, parse successfully, use a single-instance crash-recoverable updater, run local deployment preflight, exercise exact-SHA one-click bootstrap tests, and pin Windows validation to the triggering commit.' -ForegroundColor Green
