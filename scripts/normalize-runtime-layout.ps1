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
                $packages += [pscustomobject]@{
                    Relative = "$($entry.Name)\$($scoped.Name)"
                    Path = $scoped.FullName
                }
            }
        } else {
            $packages += [pscustomobject]@{
                Relative = $entry.Name
                Path = $entry.FullName
            }
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
        if ($children.Count -eq 0) {
            Remove-Item $dir.FullName -Force -ErrorAction SilentlyContinue
        }
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
$piRoot = Join-Path $Root 'Pi'
$piNodeModules = Join-Path $piRoot 'node_modules'
$piAgentRoot = Join-Path $piNodeModules '@earendil-works\pi-coding-agent'
$piCli = Join-Path $piAgentRoot 'dist\cli.js'
$dshBin = Join-Path $Root 'Runtime\Node\node_modules\@deepseek-ai\dsh\lib\bin.js'

foreach ($required in @($node, $piCli, $dshBin)) {
    if (-not (Test-Path $required -PathType Leaf)) { throw "Runtime layout normalization prerequisite is missing: $required" }
}

# npm/pnpm can leave dependencies under the scoped Pi package itself, producing
# paths such as Pi/node_modules/@earendil-works/pi-coding-agent/node_modules/...
# that exceed legacy MAX_PATH once the user chooses a normal install directory.
#
# Hoist every dependency that can be moved without changing Node resolution:
# - absent at Pi/node_modules: move it to the shallow root;
# - same version already at the shallow root: remove the duplicate;
# - different/unknown version: leave nested and let the path-budget gate report
#   whether further package-specific work is necessary.
#
# Multiple passes handle dependencies that themselves contain nested node_modules.
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

# Re-probe the real entrypoints after rearranging node_modules. This verifies
# that the optimized tree still works without relying on machine-global Node,
# PATH, CWD, junctions, or Windows LongPathsEnabled.
& $node $piCli --version | Out-Host
if ($LASTEXITCODE -ne 0) { throw 'Pi runtime failed after path normalization.' }

& $node $dshBin --help | Out-Host
if ($LASTEXITCODE -ne 0) { throw 'DeepSeek Harness runtime failed after path normalization.' }

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

Write-Host 'Runtime layout normalization passed without changing package APIs or requiring Windows long-path policy.' -ForegroundColor Green
