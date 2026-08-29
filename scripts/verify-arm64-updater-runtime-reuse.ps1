$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$updaterPath = Join-Path $root 'scripts\update-miaodesk-arm64.ps1'
if (-not (Test-Path -LiteralPath $updaterPath -PathType Leaf)) {
    throw 'ARM64 updater script is missing.'
}

$text = Get-Content -LiteralPath $updaterPath -Raw

$required = @(
    '$RuntimeStoreDir = Join-Path $DeployParent "RuntimeBundle"',
    'function New-Junction',
    'New-Item -ItemType Junction',
    'function Link-RuntimeBundle',
    'function Try-LinkUnchangedRuntimeBundle',
    'function Promote-ReusedRuntimeBundle',
    'Move-Item -LiteralPath $source',
    'Link-RuntimeBundle $StagedRoot $RuntimeStoreDir',
    'Materializing changed MiaoDesk RuntimeBundle',
    'function Remove-Junction',
    '[IO.Directory]::Delete($Path, $false)',
    'function Remove-DeploymentTree',
    'if (Test-ReparsePoint $child) { Remove-Junction $child }',
    'Remove-DeploymentTree $DeployDir',
    'Remove-DeploymentTree $previous',
    'Remove-DeploymentTree $next'
)

foreach ($marker in $required) {
    if (-not $text.Contains($marker)) {
        throw "ARM64 updater runtime-reuse contract is missing marker: $marker"
    }
}

$forbidden = @(
    'Copy-TreeLongPath',
    'robocopy.exe',
    'Copy-UnchangedRuntimeBundle'
)
foreach ($marker in $forbidden) {
    if ($text.Contains($marker)) {
        throw "ARM64 updater must not deep-copy an unchanged RuntimeBundle: $marker"
    }
}

if ($text -match 'Copy-Item[^\r\n]+\$DeployDir[^\r\n]+(?:Runtime|Pi|Goz)') {
    throw 'ARM64 updater must not recursively copy Runtime/Pi/Goz from the installed package.'
}

if ($text -match 'Copy-Item[^\r\n]+RuntimeStoreDir') {
    throw 'ARM64 updater must link the shared RuntimeBundle instead of copying it into staging.'
}

if ($text -notmatch 'if \(-not \$reusedRuntime\) \{ Materialize-Runtime') {
    throw 'ARM64 updater must materialize the RuntimeBundle only when the persisted bundle changed.'
}

$removeJunctionMatch = [regex]::Match(
    $text,
    'function Remove-Junction\(\[string\]\$Path\) \{(?<body>[\s\S]*?)\r?\n\}',
    [Text.RegularExpressions.RegexOptions]::CultureInvariant
)
if (-not $removeJunctionMatch.Success) {
    throw 'ARM64 updater junction cleanup helper could not be inspected.'
}
$removeJunctionBody = $removeJunctionMatch.Groups['body'].Value
if ($removeJunctionBody -match '(?m)^\s*Remove-Item\b') {
    throw 'Remove-Junction must never execute Windows PowerShell Remove-Item; it can prompt or traverse a non-empty junction.'
}
if ($removeJunctionBody -notmatch '\[IO\.Directory\]::Delete\(\$Path, \$false\)') {
    throw 'Remove-Junction must delete only the reparse-point directory entry with Directory.Delete(path, false).'
}

$unsafeDeploymentCleanup = [regex]::Matches(
    $text,
    'Remove-Item[^\r\n]+(?:\$DeployDir|\$previous|\$next)[^\r\n]*-Recurse',
    [Text.RegularExpressions.RegexOptions]::IgnoreCase
)
if ($unsafeDeploymentCleanup.Count -gt 0) {
    throw 'Updater deployment cleanup must detach Runtime/Pi/Goz junctions before recursively deleting a deployment tree.'
}

Write-Host 'ARM64 updater shared RuntimeBundle and prompt-free junction cleanup contract verified.' -ForegroundColor Green
