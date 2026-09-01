param(
    [Parameter(Mandatory = $true)][string]$Root
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Read-PackageVersion([string]$PackageRoot) {
    $packageJson = Join-Path $PackageRoot 'package.json'
    if (-not (Test-Path $packageJson -PathType Leaf)) { return '' }
    try { return [string]((Get-Content $packageJson -Raw | ConvertFrom-Json).version) }
    catch { return '' }
}

function Hoist-Package([string]$Nested, [string]$Hoisted, [string]$Label) {
    if (-not (Test-Path $Nested -PathType Container)) { return $false }

    if (Test-Path $Hoisted -PathType Container) {
        $nestedVersion = Read-PackageVersion $Nested
        $hoistedVersion = Read-PackageVersion $Hoisted
        if ([string]::IsNullOrWhiteSpace($nestedVersion) -or
            [string]::IsNullOrWhiteSpace($hoistedVersion) -or
            $nestedVersion -ne $hoistedVersion) {
            throw "Cannot safely hoist $Label: nested version '$nestedVersion' differs from existing root version '$hoistedVersion'."
        }
        Remove-Item $Nested -Recurse -Force
        Write-Host "Removed duplicate nested $Label $nestedVersion; root copy already exists." -ForegroundColor DarkGray
        return $true
    }

    New-Item -ItemType Directory -Force -Path (Split-Path $Hoisted -Parent) | Out-Null
    Move-Item -Path $Nested -Destination $Hoisted
    Write-Host "Hoisted $Label to shorten stock-Windows runtime paths: $Hoisted" -ForegroundColor Cyan
    return $true
}

$piAgentRoot = Join-Path $Root 'Pi\node_modules\@earendil-works\pi-coding-agent'
$nestedMistral = Join-Path $piAgentRoot 'node_modules\@mistralai\mistralai'
$hoistedMistral = Join-Path $Root 'Pi\node_modules\@mistralai\mistralai'
$changed = Hoist-Package $nestedMistral $hoistedMistral '@mistralai/mistralai'

if ($changed) {
    # Remove now-empty scoped/package-manager directories only when empty.
    foreach ($dir in @(
        (Join-Path $piAgentRoot 'node_modules\@mistralai'),
        (Join-Path $piAgentRoot 'node_modules')
    )) {
        if (Test-Path $dir -PathType Container) {
            $children = @(Get-ChildItem $dir -Force -ErrorAction SilentlyContinue)
            if ($children.Count -eq 0) { Remove-Item $dir -Force }
        }
    }
}

# Runtime sanity probe: importing from a file at Pi root exercises the same
# ancestor node_modules lookup that lets nested Pi code resolve the hoisted SDK.
$node = Join-Path $Root 'Runtime\Node\node.exe'
if (-not (Test-Path $node -PathType Leaf)) { throw "Bundled Node runtime is missing: $node" }
if (Test-Path $hoistedMistral -PathType Container) {
    $probe = Join-Path $Root 'Pi\.__miaodesk_mistral_probe.mjs'
    try {
        Set-Content -Path $probe -Encoding UTF8 -Value "import '@mistralai/mistralai';"
        & $node $probe
        if ($LASTEXITCODE -ne 0) { throw 'Hoisted @mistralai/mistralai import probe failed.' }
    } finally {
        Remove-Item $probe -Force -ErrorAction SilentlyContinue
    }
}

Write-Host 'Runtime layout normalization completed without changing package APIs or requiring Windows long-path policy.' -ForegroundColor Green
