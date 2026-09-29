param(
  [string]$RunRoot = 'C:\MiaoDeskDev',
  [switch]$NoLaunch
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$core = Join-Path $PSScriptRoot 'quick-test-arm64-core.ps1'
if (-not (Test-Path $core -PathType Leaf)) { throw "Missing ARM64 quick-test core: $core" }

# The core owns Runtime/AI/Goz bootstrap and ordinary delta updates. Keep it from
# launching until this wrapper has guaranteed that a first-time baseline also has
# the exact current-SHA product overlay.
& $core -RunRoot $RunRoot -NoLaunch
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$statePath = Join-Path $RunRoot '.miaodesk-dev-state.json'
if (-not (Test-Path $statePath -PathType Leaf)) { throw 'ARM64 dev state was not created.' }
$state = Get-Content $statePath -Raw | ConvertFrom-Json
$sha = (& git rev-parse HEAD).Trim()
if ([string]$state.installedSha -ne $sha) { throw "ARM64 state SHA mismatch: $($state.installedSha) != $sha" }

$needsExactOverlay = ([string]$state.mode -ne 'delta')
if ($needsExactOverlay) {
  $gh = Get-Command gh.exe -ErrorAction Stop
  $repo = 'GoodLoongStudio/MiaoDesk'
  $artifactName = "MiaoDesk-arm64-fast-$sha"

  Write-Host ""
  Write-Host "==> Apply exact current-SHA ARM64 overlay" -ForegroundColor Cyan

  $selectedRun = $null
  for ($i=0; $i -lt 120; $i++) {
    $json = & $gh.Source run list --repo $repo --workflow fast-dev-arm64.yml --branch main --limit 30 --json databaseId,headSha,status,conclusion
    if ($LASTEXITCODE -ne 0) { throw 'Unable to list Fast ARM64 workflow runs.' }
    $runs = @($json | ConvertFrom-Json | Where-Object { $_.headSha -eq $sha } | Sort-Object databaseId -Descending)

    foreach ($run in @($runs | Where-Object { $_.status -eq 'completed' -and $_.conclusion -eq 'success' })) {
      $names = @(& $gh.Source api "repos/$repo/actions/runs/$($run.databaseId)/artifacts" --jq '.artifacts[] | select(.expired == false) | .name' 2>$null)
      if ($names -contains $artifactName) { $selectedRun = $run; break }
    }
    if ($selectedRun) { break }

    $latest = $runs | Select-Object -First 1
    if ($latest -and $latest.status -eq 'completed' -and $latest.conclusion -notin @('success','cancelled')) {
      throw "Fast ARM64 build failed. Run $($latest.databaseId): $($latest.conclusion)"
    }
    if ($i % 6 -eq 0) { Write-Host 'Waiting for exact ARM64 overlay...' }
    Start-Sleep -Seconds 5
  }
  if (-not $selectedRun) { throw "Timed out waiting for $artifactName" }

  $temp = Join-Path $env:TEMP ("MiaoDeskOverlay-" + [Guid]::NewGuid().ToString('N'))
  New-Item -ItemType Directory -Force -Path $temp | Out-Null
  try {
    & $gh.Source run download $selectedRun.databaseId --repo $repo --name $artifactName --dir $temp
    if ($LASTEXITCODE -ne 0) { throw "Unable to download $artifactName" }
    $exe = Get-ChildItem $temp -Filter MiaoDesk.exe -File -Recurse | Select-Object -First 1
    if (-not $exe) { throw 'Downloaded ARM64 overlay has no MiaoDesk.exe.' }
    $source = $exe.Directory.FullName

    foreach ($name in @('MiaoDesk','MiaoDeskWallpaper','MiaoDeskHarness')) {
      Get-Process $name -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    }

    # Product-owned overlay directories are replaceable. Runtime/AI/Goz are the
    # persistent baseline and are intentionally not touched here.
    foreach ($dir in @('Assets','Config','Wallpapers','Widgets','skills')) {
      Remove-Item (Join-Path $RunRoot $dir) -Recurse -Force -ErrorAction SilentlyContinue
    }
    foreach ($file in @('MiaoDesk.exe','MiaoDeskWallpaper.exe','MiaoDeskHarness.exe','LICENSE','THIRD-PARTY-NOTICES.md')) {
      Remove-Item (Join-Path $RunRoot $file) -Force -ErrorAction SilentlyContinue
    }

    robocopy $source $RunRoot /E /R:2 /W:1 /NFL /NDL /NJH /NJS /NP | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "ARM64 overlay copy failed: $LASTEXITCODE" }

    foreach ($exeName in @('MiaoDesk.exe','MiaoDeskWallpaper.exe','MiaoDeskHarness.exe')) {
      $p = Start-Process (Join-Path $RunRoot $exeName) -ArgumentList '--self-test' -Wait -PassThru
      if ($p.ExitCode -ne 0) { throw "$exeName --self-test failed after current overlay: $($p.ExitCode)" }
      Write-Host "[PASS] $exeName --self-test" -ForegroundColor Green
    }

    $goz = Join-Path $RunRoot 'Goz\goz.exe'
    & $goz --status *> $null
    if ($LASTEXITCODE -ne 0) { throw 'Goz file-search service is not queryable after overlay.' }

    $state.mode = 'delta'
    $state.runId = [Int64]$selectedRun.databaseId
    $state.updatedUtc = [DateTime]::UtcNow.ToString('o')
    $state | ConvertTo-Json | Set-Content $statePath -Encoding utf8
  } finally {
    Remove-Item $temp -Recurse -Force -ErrorAction SilentlyContinue
  }
}

if (-not $NoLaunch) {
  Start-Process (Join-Path $RunRoot 'MiaoDesk.exe') -WorkingDirectory $RunRoot
  Write-Host "[PASS] MiaoDesk launched" -ForegroundColor Green
}
