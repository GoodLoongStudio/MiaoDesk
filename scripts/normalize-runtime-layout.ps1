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

function Get-NodeModulePackages([string]$NodeModulesRoot) {
    if (-not (Test-Path $NodeModulesRoot -PathType Container)) { return @() }

    $packages = @()
    foreach ($entry in @(Get-ChildItem $NodeModulesRoot -Directory -Force -ErrorAction SilentlyContinue)) {
        if ($entry.Name.StartsWith('.')) { continue }
        if ($entry.Name.StartsWith('@')) {
            foreach ($scoped in @(Get-ChildItem $entry.FullName -Directory -Force -ErrorAction SilentlyContinue)) {
                $packages += [pscustomobject]@{ Relative = "$($entry.Name)\$($scoped.Name)"; Path = $scoped.FullName }
            }
        } else {
            $packages += [pscustomobject]@{ Relative = $entry.Name; Path = $entry.FullName }
        }
    }
    return @($packages)
}

function Remove-EmptyNodeModuleContainers([string]$Start) {
    if (-not (Test-Path $Start -PathType Container)) { return }
    $dirs = @(Get-ChildItem $Start -Directory -Recurse -Force -ErrorAction SilentlyContinue |
        Sort-Object { $_.FullName.Length } -Descending)
    foreach ($dir in $dirs) {
        if ($dir.Name -eq '.bin') { continue }
        $children = @(Get-ChildItem $dir.FullName -Force -ErrorAction SilentlyContinue)
        if ($children.Count -eq 0) { Remove-Item $dir.FullName -Force -ErrorAction SilentlyContinue }
    }
}

function Try-HoistPackage([string]$Source, [string]$Destination, [string]$Label) {
    if (-not (Test-Path $Source -PathType Container)) { return 'missing' }
    if (Test-Path $Destination -PathType Container) {
        $sourceVersion = Read-PackageVersion $Source
        $destinationVersion = Read-PackageVersion $Destination
        if (-not [string]::IsNullOrWhiteSpace($sourceVersion) -and
            -not [string]::IsNullOrWhiteSpace($destinationVersion) -and
            $sourceVersion -eq $destinationVersion) {
            Remove-Item $Source -Recurse -Force
            Write-Host "Deduplicated nested $Label $sourceVersion; identical shallow copy already exists." -ForegroundColor DarkGray
            return 'deduped'
        }
        Write-Host "Kept nested $Label because shallow destination has a different/unknown version ($sourceVersion vs $destinationVersion)." -ForegroundColor DarkYellow
        return 'conflict'
    }
    New-Item -ItemType Directory -Force -Path (Split-Path $Destination -Parent) | Out-Null
    Move-Item -Path $Source -Destination $Destination
    Write-Host "Hoisted $Label to shorten stock-Windows runtime paths." -ForegroundColor Cyan
    return 'moved'
}

$node = Join-Path $Root 'Runtime\Node\node.exe'
$legacyNodeModules = Join-Path $Root 'Runtime\Node\node_modules'
$shallowNodeModules = Join-Path $Root 'node_modules'
$legacyDshBin = Join-Path $legacyNodeModules '@deepseek-ai\dsh\lib\bin.js'
$shallowDshBin = Join-Path $shallowNodeModules '@deepseek-ai\dsh\lib\bin.js'
$piRoot = Join-Path $Root 'Pi'
$piNodeModules = Join-Path $piRoot 'node_modules'
$piAgentRoot = Join-Path $piNodeModules '@earendil-works\pi-coding-agent'
$piCli = Join-Path $piAgentRoot 'dist\cli.js'

foreach ($required in @($node, $piCli)) {
    if (-not (Test-Path $required -PathType Leaf)) { throw "Runtime layout normalization prerequisite is missing: $required" }
}

# Keep node.exe at Runtime\Node, but move the large DSH dependency tree to the
# install-root node_modules directory. This is ordinary Node ancestor/sibling
# resolution and shortens every DSH dependency path by exactly 13 characters.
if (Test-Path $legacyNodeModules -PathType Container) {
    if (Test-Path $shallowNodeModules -PathType Container) {
        throw "Cannot normalize DSH runtime: both legacy and shallow node_modules trees exist: $legacyNodeModules ; $shallowNodeModules"
    }
    Move-Item -Path $legacyNodeModules -Destination $shallowNodeModules
    Write-Host 'Moved DSH node_modules to install root; every DSH dependency path is 13 characters shorter.' -ForegroundColor Cyan
}

if (-not (Test-Path $shallowDshBin -PathType Leaf)) {
    throw "Shallow DeepSeek Harness entrypoint is missing after runtime relocation: $shallowDshBin"
}

# Backward-compatible launcher: existing native code and older scripts may still
# invoke Runtime\Node\node_modules\@deepseek-ai\dsh\lib\bin.js. Recreate only
# that tiny entrypoint, never the deep dependency tree. CommonJS dynamic import
# works even without a package.json in this compatibility directory.
New-Item -ItemType Directory -Force -Path (Split-Path $legacyDshBin -Parent) | Out-Null
$shim = @'
(async () => {
  await import('../../../../../../node_modules/@deepseek-ai/dsh/lib/bin.js');
})().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
'@
Set-Content -Path $legacyDshBin -Encoding UTF8 -Value $shim
Write-Host 'Created tiny legacy DSH launcher shim; the real dependency tree remains shallow.' -ForegroundColor DarkGray

# Pi may also contain package-local node_modules. Hoist only when Node resolution
# semantics are preserved; conflicting versions remain nested and are reported.
$conflicts = New-Object System.Collections.Generic.List[string]
for ($pass = 1; $pass -le 8; $pass++) {
    $changed = $false
    $nestedNodeModules = @(Get-ChildItem $piAgentRoot -Directory -Recurse -Force -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -eq 'node_modules' } |
        Sort-Object { $_.FullName.Length } -Descending)
    foreach ($nodeModulesDir in $nestedNodeModules) {
        foreach ($package in @(Get-NodeModulePackages $nodeModulesDir.FullName)) {
            $destination = Join-Path $piNodeModules $package.Relative
            $result = Try-HoistPackage $package.Path $destination $package.Relative
            if ($result -eq 'moved' -or $result -eq 'deduped') { $changed = $true }
            elseif ($result -eq 'conflict' -and -not $conflicts.Contains($package.Relative)) { $conflicts.Add($package.Relative) }
        }
    }
    Remove-EmptyNodeModuleContainers $piAgentRoot
    if (-not $changed) { break }
}
if ($conflicts.Count -gt 0) {
    Write-Host "Runtime normalization preserved $($conflicts.Count) version-conflicting nested package(s): $($conflicts -join ', ')" -ForegroundColor DarkYellow
}

# Re-probe both the real shallow DSH entrypoint and the compatibility launcher.
& $node $piCli --version | Out-Host
if ($LASTEXITCODE -ne 0) { throw 'Pi runtime failed after path normalization.' }
& $node $shallowDshBin --help | Out-Host
if ($LASTEXITCODE -ne 0) { throw 'Shallow DeepSeek Harness runtime failed after path normalization.' }
& $node $legacyDshBin --help | Out-Host
if ($LASTEXITCODE -ne 0) { throw 'Legacy DeepSeek Harness launcher shim failed after path normalization.' }

$hoistedMistral = Join-Path $piNodeModules '@mistralai\mistralai'
if (Test-Path $hoistedMistral -PathType Container) {
    $probe = Join-Path $piRoot '.__miaodesk_mistral_probe.mjs'
    try {
        Set-Content -Path $probe -Encoding UTF8 -Value "import '@mistralai/mistralai';"
        & $node $probe
        if ($LASTEXITCODE -ne 0) { throw 'Hoisted @mistralai/mistralai dependency probe failed.' }
    } finally {
        Remove-Item $probe -Force -ErrorAction SilentlyContinue
    }
}

Write-Host 'Runtime layout normalization passed with shallow DSH dependencies and no Windows long-path policy dependency.' -ForegroundColor Green
