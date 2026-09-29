param(
  [string]$SourceRoot = (Split-Path $PSScriptRoot -Parent),
  [string]$BuildRoot = 'C:\b\MiaoDesk\arm64',
  [string]$RunRoot = 'C:\pkg\MiaoDesk\arm64-dev',
  [switch]$NoPull,
  [switch]$SkipSelfTest,
  [switch]$NoLaunch
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
function Step($m){Write-Host "";Write-Host "==> $m" -ForegroundColor Cyan}
function Pass($m){Write-Host "[PASS] $m" -ForegroundColor Green}
$arch=[Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()
if($arch -ne 'Arm64'){throw "quick-test-arm64 requires Windows ARM64; detected $arch"}
Set-Location $SourceRoot

if(-not $NoPull){
  Step 'Update main'
  $branch=(git branch --show-current).Trim()
  if($branch -ne 'main'){throw "Current branch is '$branch'. Switch to main or use -NoPull intentionally."}
  git pull --ff-only
  if($LASTEXITCODE -ne 0){throw 'git pull --ff-only failed'}
}

if(-not(Test-Path (Join-Path $BuildRoot 'CMakeCache.txt'))){
  Step 'First-time ARM64 configure'
  New-Item -ItemType Directory -Force -Path (Split-Path $BuildRoot -Parent)|Out-Null
  cmake -S $SourceRoot -B $BuildRoot -A ARM64 -DCMAKE_INSTALL_PREFIX="$RunRoot"
  if($LASTEXITCODE -ne 0){throw 'ARM64 configure failed'}
}

Step 'Incremental build'
cmake --build $BuildRoot --config Release --target MiaoDesk MiaoDeskWallpaper MiaoDeskHarness --parallel
if($LASTEXITCODE -ne 0){throw 'Incremental ARM64 build failed'}
Pass 'Incremental build'

Step 'Refresh local runnable tree'
New-Item -ItemType Directory -Force -Path $RunRoot|Out-Null
cmake --install $BuildRoot --config Release --prefix $RunRoot
if($LASTEXITCODE -ne 0){throw 'cmake --install failed'}
& (Join-Path $SourceRoot 'packaging\windows\stage.ps1') -Destination $RunRoot -Architecture arm64
if($LASTEXITCODE -ne 0){throw 'Runtime staging failed'}
Pass "Runnable tree: $RunRoot"

if(-not $SkipSelfTest){
  Step 'Fast self-tests'
  foreach($e in @('MiaoDesk.exe','MiaoDeskWallpaper.exe','MiaoDeskHarness.exe')){
    $p=Start-Process (Join-Path $RunRoot $e) -ArgumentList '--self-test' -Wait -PassThru
    if($p.ExitCode -ne 0){throw "$e --self-test failed: $($p.ExitCode)"}
    Pass "$e --self-test"
  }
}

if(-not $NoLaunch){
  Step 'Restart MiaoDesk from dev tree'
  foreach($name in @('MiaoDesk','MiaoDeskWallpaper','MiaoDeskHarness')){
    Get-Process $name -ErrorAction SilentlyContinue|Stop-Process -Force -ErrorAction SilentlyContinue
  }
  Start-Process (Join-Path $RunRoot 'MiaoDesk.exe') -WorkingDirectory $RunRoot
  Pass 'MiaoDesk launched'
}
Write-Host ""
Write-Host "QUICK ARM64 TEST READY" -ForegroundColor Green
Write-Host "Build: $BuildRoot"
Write-Host "Run:   $RunRoot"
