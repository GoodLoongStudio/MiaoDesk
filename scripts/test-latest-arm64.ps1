param(
    [string]$InstallerPath,
    [string]$InstallDir = "$env:ProgramFiles\MiaoDesk",
    [switch]$SkipLaunch,
    [switch]$KeepDownloadedInstaller
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
function Step([string]$m){ Write-Host ""; Write-Host "==> $m" -ForegroundColor Cyan }
function Pass([string]$m){ Write-Host "[PASS] $m" -ForegroundColor Green }
function Fail([string]$m){ throw "[FAIL] $m" }

$arch=[System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()
if($arch -ne 'Arm64'){ Fail "Requires Windows ARM64; detected $arch" }
Pass "Windows ARM64 host"

$tempRoot=$null
try {
  if($InstallerPath){
    $installer=(Resolve-Path -LiteralPath $InstallerPath).Path
  } else {
    $gh=Get-Command gh.exe -ErrorAction SilentlyContinue
    if(-not $gh){ Fail "GitHub CLI (gh) is required for auto-download. Install gh and run gh auth login, or pass -InstallerPath." }
    Step "Find latest successful Windows ARM64 Package on main"
    $json=& $gh.Source run list --repo GoodLoongStudio/MiaoDesk --workflow "Windows ARM64 Package" --branch main --status success --limit 1 --json databaseId,headSha
    if($LASTEXITCODE -ne 0){ Fail "Cannot query GitHub Actions." }
    $run=@($json|ConvertFrom-Json)[0]
    if(-not $run){ Fail "No successful ARM64 package run found." }
    Write-Host "Run $($run.databaseId), commit $($run.headSha)"
    $a=& $gh.Source api "repos/GoodLoongStudio/MiaoDesk/actions/runs/$($run.databaseId)/artifacts"
    if($LASTEXITCODE -ne 0){ Fail "Cannot query artifacts." }
    $artifact=@((($a|ConvertFrom-Json).artifacts)|Where-Object{-not $_.expired -and $_.name -like 'MiaoDesk-windows-arm64-installer-*'}|Sort-Object created_at -Descending)[0]
    if(-not $artifact){ Fail "No live ARM64 installer artifact found." }
    $tempRoot=Join-Path $env:TEMP ("MiaoDesk-QuickDeploy-"+[Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Force -Path $tempRoot|Out-Null
    Step "Download $($artifact.name)"
    & $gh.Source api "repos/GoodLoongStudio/MiaoDesk/actions/artifacts/$($artifact.id)/zip" > (Join-Path $tempRoot 'installer.zip')
    if($LASTEXITCODE -ne 0){ Fail "Artifact download failed." }
    Expand-Archive (Join-Path $tempRoot 'installer.zip') $tempRoot -Force
    $installer=(Get-ChildItem $tempRoot -Filter 'MiaoDesk-arm64-Setup-*-UTC8.exe' -File -Recurse|Select-Object -First 1).FullName
    if(-not $installer){ Fail "Expected UTC8 ARM64 installer not found in artifact." }
    Pass "Downloaded $([IO.Path]::GetFileName($installer))"
  }

  Step "Silent install to $InstallDir"
  $p=Start-Process $installer -ArgumentList @('/S',"/D=$InstallDir") -Verb RunAs -Wait -PassThru
  if($p.ExitCode -ne 0){ Fail "Installer exit code $($p.ExitCode)" }

  foreach($f in @('MiaoDesk.exe','MiaoDeskWallpaper.exe','MiaoDeskHarness.exe','Goz\goz.exe','Goz\gozd.exe','Uninstall.exe')){
    if(-not(Test-Path (Join-Path $InstallDir $f) -PathType Leaf)){ Fail "Missing installed file: $f" }
  }
  Pass "Core installed files"

  Step "Product self-tests"
  foreach($e in @('MiaoDesk.exe','MiaoDeskWallpaper.exe','MiaoDeskHarness.exe')){
    $p=Start-Process (Join-Path $InstallDir $e) -ArgumentList '--self-test' -Wait -PassThru
    if($p.ExitCode -ne 0){ Fail "$e self-test exit $($p.ExitCode)" }
    Pass "$e --self-test"
  }

  Step "goz health and real filename lookup"
  $goz=Join-Path $InstallDir 'Goz\goz.exe'
  $ready=$false
  for($i=0;$i -lt 30;$i++){ & $goz --status *> $null; if($LASTEXITCODE -eq 0){$ready=$true;break}; Start-Sleep 1 }
  if(-not $ready){ Fail "goz service is not queryable." }
  Pass "goz service"

  $probeDir=Join-Path $env:TEMP 'MiaoDesk-QuickDeploy-Probe'
  New-Item -ItemType Directory -Force -Path $probeDir|Out-Null
  $probeName="miaodesk-quick-$([Guid]::NewGuid().ToString('N')).txt"
  try {
    Set-Content (Join-Path $probeDir $probeName) 'MiaoDesk quick deploy smoke'
    $found=$false
    for($i=0;$i -lt 30;$i++){
      $out=@(& $goz -n 8 $probeName 2>&1)
      $text=($out|ForEach-Object{"$_"}) -join [Environment]::NewLine
      if($text.IndexOf($probeName,[StringComparison]::OrdinalIgnoreCase)-ge 0){$found=$true;break}
      Start-Sleep 1
    }
    if(-not $found){ Fail "Real file lookup did not return the unique probe file." }
    Pass "Real file search"
  } finally { Remove-Item $probeDir -Recurse -Force -ErrorAction SilentlyContinue }

  if(-not $SkipLaunch){
    Step "Launch MiaoDesk"
    Start-Process (Join-Path $InstallDir 'MiaoDesk.exe') -WorkingDirectory $InstallDir
    Pass "MiaoDesk launched"
  }
  Write-Host ""; Write-Host "QUICK DEPLOY PASS" -ForegroundColor Green
  Write-Host "Installer: $([IO.Path]::GetFileName($installer))"
  Write-Host "Install:   $InstallDir"
} finally {
  if($tempRoot -and -not $KeepDownloadedInstaller){ Remove-Item $tempRoot -Recurse -Force -ErrorAction SilentlyContinue }
}
