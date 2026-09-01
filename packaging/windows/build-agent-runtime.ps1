param(
    [Parameter(Mandatory = $true)][string]$Root,
    [Parameter(Mandatory = $true)][ValidateSet('x64','arm64')][string]$Architecture
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$ProgressPreference = 'SilentlyContinue'

$RepoRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$LockPath = Join-Path $RepoRoot ("runtime\{0}\runtime-lock.json" -f $Architecture)
if (-not (Test-Path $LockPath -PathType Leaf)) { throw "Runtime lock is missing: $LockPath" }

$lock = Get-Content $LockPath -Raw | ConvertFrom-Json
if ([string]$lock.architecture -ne $Architecture) {
    throw "Runtime lock architecture mismatch: expected=$Architecture actual=$($lock.architecture)"
}

$nodeDir = Join-Path $Root 'Runtime\Node'
$nodeExe = Join-Path $nodeDir 'node.exe'
$npmCmd = Join-Path $nodeDir 'npm.cmd'
foreach ($required in @($nodeExe, $npmCmd)) {
    if (-not (Test-Path $required -PathType Leaf)) { throw "Agent runtime prerequisite is missing: $required" }
}

$agentRoot = Join-Path $Root 'Runtime\Agent'
Remove-Item $agentRoot -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $agentRoot | Out-Null

$dshPackage = [string]$lock.deepseekHarness.package
$dshVersion = [string]$lock.deepseekHarness.version
$piPackage = [string]$lock.pi.package
$piVersion = [string]$lock.pi.version
if ([string]::IsNullOrWhiteSpace($dshPackage)) { $dshPackage = '@deepseek-ai/dsh' }
if ([string]::IsNullOrWhiteSpace($piPackage)) { $piPackage = '@earendil-works/pi-coding-agent' }
if ([string]::IsNullOrWhiteSpace($dshVersion) -or [string]::IsNullOrWhiteSpace($piVersion)) {
    throw 'Agent runtime requires pinned DSH and Pi versions.'
}

$package = [ordered]@{
    name = 'miaodesk-agent-runtime'
    private = $true
    version = '1.0.0'
    dependencies = [ordered]@{
        $dshPackage = $dshVersion
        $piPackage = $piVersion
    }
}
$package | ConvertTo-Json -Depth 6 | Set-Content (Join-Path $agentRoot 'package.json') -Encoding UTF8

$cacheBase = if ([string]::IsNullOrWhiteSpace($env:RUNNER_TEMP)) { $env:TEMP } else { $env:RUNNER_TEMP }
$cacheRoot = Join-Path $cacheBase ("MiaoDesk-AgentNpm-{0}-{1}" -f $Architecture, [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $cacheRoot | Out-Null
$oldCache = $env:npm_config_cache
try {
    $env:npm_config_cache = $cacheRoot
    & $npmCmd install --prefix $agentRoot --omit=dev --no-audit --no-fund --save-exact --package-lock=true --install-strategy=hoisted
    if ($LASTEXITCODE -ne 0) { throw 'MiaoDesk Agent npm install failed.' }
} finally {
    $env:npm_config_cache = $oldCache
    Remove-Item $cacheRoot -Recurse -Force -ErrorAction SilentlyContinue
}

$lockFile = Join-Path $agentRoot 'package-lock.json'
$dshBin = Join-Path $agentRoot 'node_modules\@deepseek-ai\dsh\lib\bin.js'
$piCli = Join-Path $agentRoot 'node_modules\@earendil-works\pi-coding-agent\dist\cli.js'
foreach ($required in @($lockFile, $dshBin, $piCli)) {
    if (-not (Test-Path $required -PathType Leaf)) { throw "Agent runtime is incomplete: $required" }
}

& $nodeExe $dshBin --help | Out-Host
if ($LASTEXITCODE -ne 0) { throw 'DeepSeek Harness CLI probe failed.' }
& $nodeExe $piCli --version | Out-Host
if ($LASTEXITCODE -ne 0) { throw 'Pi CLI probe failed.' }

$lockHash = (Get-FileHash -Algorithm SHA256 -Path $lockFile).Hash.ToLowerInvariant()
$fileCount = @(Get-ChildItem $agentRoot -File -Recurse -Force).Count
$directoryCount = @(Get-ChildItem $agentRoot -Directory -Recurse -Force).Count
$manifest = [ordered]@{
    schema = 3
    architecture = $Architecture
    dependencyLockSha256 = $lockHash
    dsh = [ordered]@{ package=$dshPackage; version=$dshVersion; entry='node_modules/@deepseek-ai/dsh/lib/bin.js' }
    pi = [ordered]@{ package=$piPackage; version=$piVersion; entry='node_modules/@earendil-works/pi-coding-agent/dist/cli.js' }
    fileCount = $fileCount
    directoryCount = $directoryCount
}
$manifest | ConvertTo-Json -Depth 6 | Set-Content (Join-Path $agentRoot 'runtime-manifest.json') -Encoding UTF8

# Current native binaries still understand the V2 entry locations. Keep only two
# tiny transition shims; no legacy dependency tree is shipped.
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

Write-Host "Agent runtime ready: files=$fileCount directories=$directoryCount lock=$lockHash" -ForegroundColor Green
