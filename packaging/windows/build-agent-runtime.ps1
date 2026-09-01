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

$nodeDir = Join-Path $Root 'Runtime\Node'
$nodeExe = Join-Path $nodeDir 'node.exe'
$npmCmd = Join-Path $nodeDir 'npm.cmd'
foreach ($required in @($nodeExe,$npmCmd)) {
    if (-not (Test-Path $required -PathType Leaf)) { throw "Agent runtime prerequisite is missing: $required" }
}

$agentRoot = Join-Path $Root 'Runtime\Agent'
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

$dshBin = Join-Path $agentRoot 'node_modules\@deepseek-ai\dsh\lib\bin.js'
$piCli = Join-Path $agentRoot 'node_modules\@earendil-works\pi-coding-agent\dist\cli.js'
foreach ($required in @($dshBin,$piCli)) {
    if (-not (Test-Path $required -PathType Leaf)) { throw "Agent runtime is incomplete: $required" }
}

& $nodeExe $dshBin --help | Out-Host
if ($LASTEXITCODE -ne 0) { throw 'DeepSeek Harness CLI probe failed.' }
& $nodeExe $piCli --version | Out-Host
if ($LASTEXITCODE -ne 0) { throw 'Pi CLI probe failed.' }

# Temporary compatibility entrypoints for native code that still understands V2
# locations. They contain no dependency tree and will be removed after native
# path resolution is switched fully to Runtime/Agent.
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
  await import('../../../../../Runtime/Agent/node_modules/@earendil-works/pi-coding-agent/dist/cli.js');
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
