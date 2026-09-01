param(
    [Parameter(Mandatory = $true)][string]$Root,
    [Parameter(Mandatory = $true)][ValidateSet('x64','arm64')][string]$Architecture
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$ProgressPreference = 'SilentlyContinue'

$RepoRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$RuntimeLockPath = Join-Path $RepoRoot ("runtime\{0}\runtime-lock.json" -f $Architecture)
$AgentSource = Join-Path $RepoRoot 'runtime\agent'
$AgentPackage = Join-Path $AgentSource 'package.json'
$AgentLock = Join-Path $AgentSource 'package-lock.json'
foreach ($required in @($RuntimeLockPath,$AgentPackage,$AgentLock)) {
    if (-not (Test-Path $required -PathType Leaf)) { throw "Runtime input is missing: $required" }
}

$runtimeLock = Get-Content $RuntimeLockPath -Raw | ConvertFrom-Json
if ([string]$runtimeLock.architecture -ne $Architecture) {
    throw "Runtime lock architecture mismatch: expected=$Architecture actual=$($runtimeLock.architecture)"
}
$package = Get-Content $AgentPackage -Raw | ConvertFrom-Json
$dshVersion = [string]$package.dependencies.'@deepseek-ai/dsh'
$piVersion = [string]$package.dependencies.'@earendil-works/pi-coding-agent'
if ([string]::IsNullOrWhiteSpace($dshVersion) -or [string]::IsNullOrWhiteSpace($piVersion)) {
    throw 'runtime/agent/package.json must pin DSH and Pi.'
}

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
    @($packages)
}

function Remove-EmptyDirectories([string]$Start) {
    if (-not (Test-Path $Start -PathType Container)) { return }
    foreach ($dir in @(Get-ChildItem $Start -Directory -Recurse -Force -ErrorAction SilentlyContinue |
        Sort-Object { $_.FullName.Length } -Descending)) {
        if ($dir.Name -eq '.bin') { continue }
        if (@(Get-ChildItem $dir.FullName -Force -ErrorAction SilentlyContinue).Count -eq 0) {
            Remove-Item $dir.FullName -Force -ErrorAction SilentlyContinue
        }
    }
}

$nodeDir = Join-Path $Root 'Runtime\Node'
$nodeExe = Join-Path $nodeDir 'node.exe'
$npmCmd = Join-Path $nodeDir 'npm.cmd'
foreach ($required in @($nodeExe,$npmCmd)) {
    if (-not (Test-Path $required -PathType Leaf)) { throw "Agent runtime prerequisite is missing: $required" }
}

$agentRoot = Join-Path $Root 'Agent'
Remove-Item $agentRoot -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $agentRoot | Out-Null
Copy-Item $AgentPackage (Join-Path $agentRoot 'package.json') -Force
Copy-Item $AgentLock (Join-Path $agentRoot 'package-lock.json') -Force

$cacheBase = if ([string]::IsNullOrWhiteSpace($env:RUNNER_TEMP)) { $env:TEMP } else { $env:RUNNER_TEMP }
$cacheRoot = Join-Path $cacheBase ("MiaoDesk-AgentNpm-{0}-{1}" -f $Architecture, [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $cacheRoot | Out-Null
$oldCache = $env:npm_config_cache
try {
    $env:npm_config_cache = $cacheRoot
    & $npmCmd ci --prefix $agentRoot --omit=dev --no-audit --no-fund --install-strategy=hoisted
    if ($LASTEXITCODE -ne 0) { throw 'MiaoDesk Agent npm ci failed.' }
    & $npmCmd dedupe --prefix $agentRoot --omit=dev --no-audit --no-fund --package-lock=false
    if ($LASTEXITCODE -ne 0) { throw 'MiaoDesk Agent npm dedupe failed.' }
} finally {
    $env:npm_config_cache = $oldCache
    Remove-Item $cacheRoot -Recurse -Force -ErrorAction SilentlyContinue
}

$agentModules = Join-Path $agentRoot 'node_modules'
$piInstalledRoot = Join-Path $agentModules '@earendil-works\pi-coding-agent'
$dshBin = Join-Path $agentModules '@deepseek-ai\dsh\lib\bin.js'
$piInstalledCli = Join-Path $piInstalledRoot 'dist\cli.js'
foreach ($required in @($dshBin,$piInstalledCli)) {
    if (-not (Test-Path $required -PathType Leaf)) { throw "Agent runtime is incomplete: $required" }
}

# pi-coding-agent publishes an npm shrinkwrap, so npm can legally leave exact
# package copies nested below the Pi package even in a hoisted workspace. Move a
# nested package only when the shallow slot is empty; remove it only when an
# identical version already exists. Version conflicts stay nested.
$conflicts = New-Object System.Collections.Generic.List[string]
for ($pass = 1; $pass -le 8; $pass++) {
    $changed = $false
    $nestedRoots = @(Get-ChildItem $piInstalledRoot -Directory -Recurse -Force -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -eq 'node_modules' } |
        Sort-Object { $_.FullName.Length } -Descending)
    foreach ($nestedRoot in $nestedRoots) {
        foreach ($nested in @(Get-NodeModulePackages $nestedRoot.FullName)) {
            $shallow = Join-Path $agentModules $nested.Relative
            if (Test-Path $shallow -PathType Container) {
                $nestedVersion = Read-PackageVersion $nested.Path
                $shallowVersion = Read-PackageVersion $shallow
                if ($nestedVersion -and $shallowVersion -and $nestedVersion -eq $shallowVersion) {
                    Remove-Item $nested.Path -Recurse -Force
                    $changed = $true
                } elseif (-not $conflicts.Contains($nested.Relative)) {
                    $conflicts.Add($nested.Relative)
                }
                continue
            }
            New-Item -ItemType Directory -Force -Path (Split-Path $shallow -Parent) | Out-Null
            Move-Item $nested.Path $shallow
            $changed = $true
        }
    }
    Remove-EmptyDirectories $piInstalledRoot
    if (-not $changed) { break }
}
if ($conflicts.Count -gt 0) {
    Write-Host "Preserved $($conflicts.Count) nested version conflict(s): $($conflicts -join ', ')" -ForegroundColor DarkYellow
}

# TypeScript declarations and JavaScript source maps are development metadata;
# the shipped Node runtime never loads them. Removing them also keeps generated
# SDK filenames inside the stock-Windows path budget.
$runtimeMetadata = @(Get-ChildItem $agentModules -Recurse -Force -File -ErrorAction SilentlyContinue |
    Where-Object {
        $_.Name -like '*.d.ts' -or
        $_.Name -like '*.d.ts.map' -or
        $_.Name -like '*.js.map' -or
        $_.Name -like '*.mjs.map' -or
        $_.Name -like '*.cjs.map'
    })
foreach ($metadata in $runtimeMetadata) { Remove-Item $metadata.FullName -Force }
if ($runtimeMetadata.Count -gt 0) {
    Write-Host "Pruned $($runtimeMetadata.Count) runtime declaration/source-map file(s)." -ForegroundColor DarkGray
}

# The published Pi shrinkwrap preserves a nested dependency graph. Its scoped
# physical directory name alone consumes 31 characters, even though Node package
# self-references use the name in package.json rather than the folder name. Keep
# that package name intact, but shorten only its shipped physical directory.
$piRoot = Join-Path $agentModules 'pi'
Remove-Item $piRoot -Recurse -Force -ErrorAction SilentlyContinue
Move-Item $piInstalledRoot $piRoot
$piCli = Join-Path $piRoot 'dist\cli.js'
if (-not (Test-Path $piCli -PathType Leaf)) { throw "Short Pi runtime is incomplete: $piCli" }

$piBinRoot = Join-Path $agentModules '.bin'
foreach ($name in @('pi','pi.cmd','pi.ps1')) {
    $shim = Join-Path $piBinRoot $name
    if (-not (Test-Path $shim -PathType Leaf)) { throw "Pi npm shim is missing: $shim" }
    $shimText = Get-Content $shim -Raw
    $shimText = $shimText.Replace('@earendil-works/pi-coding-agent', 'pi')
    $shimText = $shimText.Replace('@earendil-works\pi-coding-agent', 'pi')
    Set-Content -Path $shim -Encoding UTF8 -NoNewline -Value $shimText
    if ((Get-Content $shim -Raw) -match '@earendil-works[\\/]pi-coding-agent') {
        throw "Pi npm shim still targets the long physical directory: $shim"
    }
}

& $nodeExe $dshBin --help | Out-Host
if ($LASTEXITCODE -ne 0) { throw 'DeepSeek Harness CLI probe failed after dependency normalization.' }
& $nodeExe $piCli --version | Out-Host
if ($LASTEXITCODE -ne 0) { throw 'Pi CLI probe failed after dependency normalization.' }

# Temporary compatibility entrypoints for native code that still understands V2
# locations. They contain no dependency tree and will be removed after native
# path resolution is switched fully to Agent.
foreach ($legacy in @(
    (Join-Path $Root 'Pi'),
    (Join-Path $Root 'node_modules'),
    (Join-Path $Root 'Runtime\Node\node_modules')
)) {
    Remove-Item $legacy -Recurse -Force -ErrorAction SilentlyContinue
}

$legacyDsh = Join-Path $Root 'Runtime\Node\node_modules\@deepseek-ai\dsh\lib\bin.js'
New-Item -ItemType Directory -Force -Path (Split-Path $legacyDsh -Parent) | Out-Null
Set-Content -Path $legacyDsh -Encoding UTF8 -Value @'
(async () => {
  await import('../../../../../Agent/node_modules/@deepseek-ai/dsh/lib/bin.js');
})().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
'@

$legacyPi = Join-Path $Root 'Pi\node_modules\@earendil-works\pi-coding-agent\dist\cli.js'
New-Item -ItemType Directory -Force -Path (Split-Path $legacyPi -Parent) | Out-Null
Set-Content -Path $legacyPi -Encoding UTF8 -Value @'
(async () => {
  await import('../../../../../Agent/node_modules/pi/dist/cli.js');
})().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
'@

& $nodeExe $legacyDsh --help | Out-Host
if ($LASTEXITCODE -ne 0) { throw 'DSH transition shim failed.' }
& $nodeExe $legacyPi --version | Out-Host
if ($LASTEXITCODE -ne 0) { throw 'Pi transition shim failed.' }

$fileCount = @(Get-ChildItem $agentRoot -File -Recurse -Force).Count
$directoryCount = @(Get-ChildItem $agentRoot -Directory -Recurse -Force).Count
$lockHash = (Get-FileHash -Algorithm SHA256 -Path (Join-Path $agentRoot 'package-lock.json')).Hash.ToLowerInvariant()
Write-Host "Agent runtime ready: files=$fileCount directories=$directoryCount lock=$lockHash DSH=$dshVersion Pi=$piVersion" -ForegroundColor Green
