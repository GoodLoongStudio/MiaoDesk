param(
  [string]$RunRoot = 'C:\MiaoDeskDev',
  [switch]$NoLaunch
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
function Step($m){Write-Host "";Write-Host "==> $m" -ForegroundColor Cyan}
function Pass($m){Write-Host "[PASS] $m" -ForegroundColor Green}
try {
  $arch=[Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()
  if($arch -ne 'Arm64'){throw "Requires Windows ARM64; detected $arch"}
  $gh=Get-Command gh.exe -ErrorAction SilentlyContinue
  if(-not $gh){throw "GitHub CLI (gh) is required. Install gh once and run: gh auth login"}

  Step 'Update main and trigger Fast ARM64 cloud build'
  git pull --ff-only
  if($LASTEXITCODE -ne 0){throw 'git pull failed'}
  $sha=(git rev-parse HEAD).Trim()
  if(-not $sha){throw 'Cannot resolve current git SHA'}
  & $gh.Source workflow run fast-dev-arm64.yml --repo GoodLoongStudio/MiaoDesk --ref main
  if($LASTEXITCODE -ne 0){throw 'Cannot trigger Fast ARM64 workflow'}

  Step "Wait for cloud build of $sha"
  $run=$null
  for($i=0;$i -lt 90;$i++){
    Start-Sleep -Seconds 5
    $json=& $gh.Source run list --repo GoodLoongStudio/MiaoDesk --workflow "Windows ARM64 Fast Dev" --branch main --limit 10 --json databaseId,headSha,status,conclusion
    if($LASTEXITCODE -ne 0){continue}
    $runs=@($json|ConvertFrom-Json)
    $run=$runs|Where-Object{$_.headSha -eq $sha}|Sort-Object databaseId -Descending|Select-Object -First 1
    if(-not $run){continue}
    Write-Host "Run $($run.databaseId): $($run.status) $($run.conclusion)"
    if($run.status -eq 'completed'){
      if($run.conclusion -ne 'success'){throw "Fast ARM64 cloud build failed: $($run.conclusion). Run $($run.databaseId)"}
      break
    }
  }
  if(-not $run -or $run.status -ne 'completed'){throw 'Timed out waiting for Fast ARM64 cloud build'}

  $artifactName="MiaoDesk-arm64-fast-$sha"
  Pass "Cloud build ready: $artifactName"

  $temp=Join-Path $env:TEMP ("MiaoDeskCloudQuick-"+[Guid]::NewGuid().ToString('N'))
  New-Item -ItemType Directory -Force -Path $temp|Out-Null
  try {
    Step "Download cloud-built runnable package: $artifactName"
    $fresh=Join-Path $temp 'fresh'
    New-Item -ItemType Directory -Force -Path $fresh|Out-Null
    & $gh.Source run download $run.databaseId --repo GoodLoongStudio/MiaoDesk --name $artifactName --dir $fresh
    if($LASTEXITCODE -ne 0){throw 'Artifact download failed'}
    $source=$fresh
    $exe=Get-ChildItem $fresh -Filter MiaoDesk.exe -File -Recurse|Select-Object -First 1
    if(-not $exe){throw 'Cloud artifact has no MiaoDesk.exe'}
    $source=$exe.Directory.FullName

    Step "Hot refresh local dev runtime: $RunRoot"
    foreach($n in @('MiaoDesk','MiaoDeskWallpaper','MiaoDeskHarness')){
      Get-Process $n -ErrorAction SilentlyContinue|Stop-Process -Force -ErrorAction SilentlyContinue
    }
    New-Item -ItemType Directory -Force -Path $RunRoot|Out-Null
    robocopy $source $RunRoot /MIR /R:2 /W:1 /NFL /NDL /NJH /NJS /NP
    if($LASTEXITCODE -ge 8){throw "robocopy failed: $LASTEXITCODE"}
    Pass 'Cloud build copied locally; no installer used'

    Step 'Fast self-tests'
    foreach($e in @('MiaoDesk.exe','MiaoDeskWallpaper.exe','MiaoDeskHarness.exe')){
      $p=Start-Process (Join-Path $RunRoot $e) -ArgumentList '--self-test' -Wait -PassThru
      if($p.ExitCode -ne 0){throw "$e --self-test failed: $($p.ExitCode)"}
      Pass "$e --self-test"
    }

    if(-not $NoLaunch){
      Step 'Launch latest cloud build'
      Start-Process (Join-Path $RunRoot 'MiaoDesk.exe') -WorkingDirectory $RunRoot
      Pass 'MiaoDesk launched'
    }
    Write-Host "";Write-Host 'QUICK CLOUD TEST READY' -ForegroundColor Green
    Write-Host "Commit: $sha"
    Write-Host "Run:    $RunRoot"
  } finally { Remove-Item $temp -Recurse -Force -ErrorAction SilentlyContinue }
} catch {
  Write-Host "";Write-Host 'QUICK CLOUD TEST FAILED' -ForegroundColor Red
  Write-Host $_.Exception.Message -ForegroundColor Red
  Read-Host 'Press Enter to close'|Out-Null
  exit 1
}
