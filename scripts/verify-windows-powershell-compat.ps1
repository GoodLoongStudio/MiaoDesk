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
foreach ($cmd in @($updateCmd, $deployCmd)) { Assert-AsciiFile $cmd }

$updateText = [IO.File]::ReadAllText($updateCmd, [Text.Encoding]::ASCII)
$deployText = [IO.File]::ReadAllText($deployCmd, [Text.Encoding]::ASCII)

foreach ($forbidden in @('^|', 'Codex-first', 'Codex CLI', 'Codex Relay')) {
    if ($updateText.Contains($forbidden) -or $deployText.Contains($forbidden)) {
        throw "One-click entrypoint contains a retired or fragile marker: $forbidden"
    }
}
foreach ($required in @('update-turingdesk-arm64.ps1', '-File "%UPDATER%"')) {
    if (-not $updateText.Contains($required)) { throw "Updater bootstrap marker missing: $required" }
}
foreach ($required in @('scripts\deploy-native-arm64.ps1', 'git pull --ff-only')) {
    if (-not $deployText.Contains($required)) { throw "Deploy bootstrap marker missing: $required" }
}

Write-Host 'Windows PowerShell 5.1 compatibility OK: all PowerShell entrypoints are ASCII-only and parse successfully.' -ForegroundColor Green
