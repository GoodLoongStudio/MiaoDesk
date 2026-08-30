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
    [System.Management.Automation.Language.Parser]::ParseFile(
        $Path, [ref]$tokens, [ref]$errors) | Out-Null
    if ($errors -and $errors.Count -gt 0) {
        $messages = ($errors | ForEach-Object {
            "line $($_.Extent.StartLineNumber): $($_.Message)"
        }) -join '; '
        throw ("Windows PowerShell parser rejected {0}: {1}" -f $Path, $messages)
    }
}

# Only scripts that are actual Windows PowerShell 5.1 entrypoints need the
# ASCII/parser restriction. The L3 runtime contract is deliberately executed
# by pwsh in CMake and both cloud workflows because it validates UTF-8 product
# documentation. Keeping it in this PS5 gate would reject valid UTF-8 by design.
$pwshOnly = @('verify-l3-runtime-contract.ps1')
$powerShellScripts = @(Get-ChildItem $scriptRoot -Filter '*.ps1' -File | Sort-Object Name |
    Where-Object { $pwshOnly -notcontains $_.Name })
if ($powerShellScripts.Count -eq 0) { throw 'No Windows PowerShell scripts were found.' }

foreach ($file in $powerShellScripts) {
    Assert-AsciiFile $file.FullName
    Assert-PowerShellParses $file.FullName
}

foreach ($name in $pwshOnly) {
    $path = Join-Path $scriptRoot $name
    if (-not (Test-Path $path -PathType Leaf)) { throw "Missing pwsh-only script: $path" }
}

$updateCmd = Join-Path $root 'UPDATE-MIAODESK.cmd'
$deployCmd = Join-Path $root 'DEPLOY-NATIVE-ARM64.cmd'
foreach ($cmd in @($updateCmd, $deployCmd)) {
    Assert-AsciiFile $cmd
}

$updateText = [IO.File]::ReadAllText($updateCmd, [Text.Encoding]::ASCII)
$deployText = [IO.File]::ReadAllText($deployCmd, [Text.Encoding]::ASCII)

foreach ($required in @(
    'update-miaodesk-arm64.ps1',
    'TD_BOOTSTRAP_SELF_TEST',
    'TD_UPDATE_URL'
)) {
    if (-not $updateText.Contains($required)) {
        throw "Updater bootstrap marker missing: $required"
    }
}

foreach ($required in @(
    'scripts\deploy-native-arm64.ps1',
    'git pull --ff-only'
)) {
    if (-not $deployText.Contains($required)) {
        throw "Deploy bootstrap marker missing: $required"
    }
}

Write-Host 'Windows PowerShell 5.1 compatibility OK: PS5 entrypoints parse and remain ASCII-safe; pwsh-only contracts are excluded explicitly.' -ForegroundColor Green
