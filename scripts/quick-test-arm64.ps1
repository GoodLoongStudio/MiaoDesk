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

  Step 'Find latest successful ARM64 cloud build'
  $json=& $gh.Source run list --repo GoodLoongStudio/MiaoDesk --workflow "Windows ARM64 Package" --branch main --status success --limit 1 --json databaseId,headSha,createdAt
  if($LASTEXITCODE -ne 0){throw 'Cannot query GitHub Actions'}
  $run=@($json|ConvertFrom-Json)[0]
  if(-not $run){throw 'No successful ARM64 cloud build found'}
  Write-Host "Run $($run.databaseId) / $($run.headSha)"

  $a=& $gh.Source api "repos/GoodLoongStudio/MiaoDesk/actions/runs/$($run.databaseId)/artifacts"
  if($LASTEXITCODE -ne 0){throw 'Cannot query artifacts'}
  $artifactResponse=$a|ConvertFrom-Json
  $allArtifacts=@($artifactResponse.artifacts)
  Write-Host "Artifacts:"
  $allArtifacts|ForEach-Object{Write-Host "  - $($_.name)"}
  $artifact=$allArtifacts|Where-Object{
    -not $_.expired -and
    $_.name -like 'MiaoDesk-windows-arm64-*' -and
    $_.name -notlike '*installer*'
  }|Sort-Object created_at -Descending|Select-Object -First 1
  if(-not $artifact){throw 'No runnable ARM64 package artifact found in this successful run'}
  Pass "Selected artifact: $($artifact.name)"

  $temp=Join-Path $env:TEMP ("MiaoDeskCloudQuick-"+[Guid]::NewGuid().ToString('N'))
  New-Item -ItemType Directory -Force -Path $temp|Out-Null
  try {
    Step "Download cloud-built runnable package: $($artifact.name)"
    $zip=Join-Path $temp 'package.zip'
    & $gh.Source api "repos/GoodLoongStudio/MiaoDesk/actions/artifacts/$($artifact.id)/zip" > $zip
    if($LASTEXITCODE -ne 0){throw 'Artifact download failed'}
    $fresh=Join-Path $temp 'fresh'
    Expand-Archive $zip $fresh -Force

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
    Write-Host "Commit: $($run.headSha)"
    Write-Host "Run:    $RunRoot"
  } finally { Remove-Item $temp -Recurse -Force -ErrorAction SilentlyContinue }
} catch {
  Write-Host "";Write-Host 'QUICK CLOUD TEST FAILED' -ForegroundColor Red
  Write-Host $_.Exception.Message -ForegroundColor Red
  Read-Host 'Press Enter to close'|Out-Null
  exit 1
}
