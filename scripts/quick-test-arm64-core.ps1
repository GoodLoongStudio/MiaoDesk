param(
  [string]$RunRoot = 'C:\MiaoDeskDev',
  [switch]$NoLaunch
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$Repo = 'GoodLoongStudio/MiaoDesk'
$StatePath = Join-Path $RunRoot '.miaodesk-dev-state.json'

function Step([string]$m) { Write-Host ""; Write-Host "==> $m" -ForegroundColor Cyan }
function Pass([string]$m) { Write-Host "[PASS] $m" -ForegroundColor Green }

function Get-BaselineKey([string]$Ref) {
  foreach ($path in @(
    'runtime/arm64',
    'runtime/agent',
    'CMakeLists.txt',
    'packaging/windows/prepare-runtime-base.ps1',
    'packaging/windows/build-agent-runtime.ps1',
    'packaging/windows/stage.ps1',
    'packaging/windows/verify-no-bundled-local-models.ps1'
  )) {
    & git cat-file -e "${Ref}:$path" 2>$null
    if ($LASTEXITCODE -ne 0) { throw "Cannot read baseline input $path at $Ref" }
  }
  $parts = @(
    (& git rev-parse "${Ref}:runtime/arm64").Trim(),
    (& git rev-parse "${Ref}:runtime/agent").Trim(),
    (& git rev-parse "${Ref}:CMakeLists.txt").Trim(),
    (& git rev-parse "${Ref}:packaging/windows/prepare-runtime-base.ps1").Trim(),
    (& git rev-parse "${Ref}:packaging/windows/build-agent-runtime.ps1").Trim(),
    (& git rev-parse "${Ref}:packaging/windows/stage.ps1").Trim(),
    (& git rev-parse "${Ref}:packaging/windows/verify-no-bundled-local-models.ps1").Trim()
  )
  $bytes = [Text.Encoding]::UTF8.GetBytes(($parts -join "`n"))
  $h = [Security.Cryptography.SHA256]::Create()
  try { return ([BitConverter]::ToString($h.ComputeHash($bytes))).Replace('-','').ToLowerInvariant() }
  finally { $h.Dispose() }
}

function Ensure-Commit([string]$Sha) {
  & git cat-file -e "$Sha^{commit}" 2>$null
  if ($LASTEXITCODE -eq 0) { return }
  & git fetch --quiet origin $Sha
  if ($LASTEXITCODE -ne 0) { throw "Cannot fetch commit $Sha" }
}

function Get-Runs([string]$Workflow,[int]$Limit=30) {
  $json = @(& $gh.Source run list --repo $Repo --workflow $Workflow --branch main --limit $Limit --json databaseId,headSha,status,conclusion,createdAt)
  if ($LASTEXITCODE -ne 0) { throw "Cannot list workflow runs: $Workflow" }

  # Windows PowerShell 5.1 can preserve a top-level JSON array as one Object[]
  # pipeline object. Parse first, then enumerate explicitly so every caller gets
  # one workflow-run object at a time on both Windows PowerShell 5.1 and pwsh 7.
  $parsed = ConvertFrom-Json -InputObject ($json -join "`n")
  foreach ($item in $parsed) { Write-Output $item }
}

function Get-RunId($Run) {
  $ids = @($Run.databaseId)
  if ($ids.Count -ne 1) {
    throw "Expected exactly one workflow run ID, got $($ids.Count): $($ids -join ', ')"
  }
  try { return [Int64]$ids[0] }
  catch { throw "Invalid workflow run ID: $($ids[0])" }
}

function Get-ArtifactNames([Int64]$RunId) {
  @(& $gh.Source api "repos/$Repo/actions/runs/$RunId/artifacts" --jq '.artifacts[] | select(.expired == false) | .name' 2>$null)
}

function Wait-FastRun([string]$Sha) {
  Step "Wait for Fast ARM64 artifact: $Sha"
  for ($i=0; $i -lt 120; $i++) {
    $same = @(Get-Runs 'fast-dev-arm64.yml' 30 | Where-Object { $_.headSha -eq $Sha } | Sort-Object databaseId -Descending)
    $success = @($same | Where-Object { $_.status -eq 'completed' -and $_.conclusion -eq 'success' })
    foreach ($run in $success) {
      $name = "MiaoDesk-arm64-fast-$Sha"
      if ((Get-ArtifactNames (Get-RunId $run)) -contains $name) {
        return [pscustomobject]@{ run=$run; artifact=$name }
      }
    }
    $latest = $same | Select-Object -First 1
    if ($latest -and $latest.status -eq 'completed' -and $latest.conclusion -notin @('success','cancelled')) {
      throw "Fast ARM64 workflow failed for $Sha. Run $($latest.databaseId): $($latest.conclusion)"
    }
    if ($i % 6 -eq 0) {
      $s = if ($latest) { "$($latest.status) $($latest.conclusion)" } else { 'not visible yet' }
      Write-Host "Fast build: $s"
    }
    Start-Sleep -Seconds 5
  }
  throw "Timed out waiting for Fast ARM64 build for $Sha"
}

function Find-CompatibleFullRun([string]$WantedKey,[string]$CurrentSha) {
  $runs = @(Get-Runs 'package-windows-arm64.yml' 35 | Sort-Object databaseId -Descending)
  foreach ($run in $runs) {
    if ($run.status -ne 'completed' -or $run.conclusion -ne 'success') { continue }
    try {
      Ensure-Commit $run.headSha
      if ((Get-BaselineKey $run.headSha) -ne $WantedKey) { continue }
      $name = "MiaoDesk-windows-arm64-$($run.headSha)"
      if ((Get-ArtifactNames (Get-RunId $run)) -contains $name) {
        return [pscustomobject]@{ run=$run; artifact=$name }
      }
    } catch { continue }
  }

  $current = @($runs | Where-Object { $_.headSha -eq $CurrentSha } | Sort-Object databaseId -Descending | Select-Object -First 1)
  if ($current.Count -eq 0) {
    Step 'No compatible full baseline exists; trigger one formal ARM64 package build'
    & $gh.Source workflow run package-windows-arm64.yml --repo $Repo --ref main
    if ($LASTEXITCODE -ne 0) { throw 'Unable to trigger the one-time full ARM64 baseline build.' }
  }

  Step 'Wait for compatible full ARM64 baseline'
  for ($i=0; $i -lt 180; $i++) {
    $same = @(Get-Runs 'package-windows-arm64.yml' 20 | Where-Object { $_.headSha -eq $CurrentSha } | Sort-Object databaseId -Descending)
    foreach ($run in $same) {
      if ($run.status -eq 'completed' -and $run.conclusion -eq 'success') {
        $name = "MiaoDesk-windows-arm64-$CurrentSha"
        if ((Get-ArtifactNames (Get-RunId $run)) -contains $name) {
          return [pscustomobject]@{ run=$run; artifact=$name }
        }
      }
    }
    $latest = $same | Select-Object -First 1
    if ($latest -and $latest.status -eq 'completed' -and $latest.conclusion -notin @('success','cancelled')) {
      throw "Full ARM64 baseline build failed. Run $($latest.databaseId): $($latest.conclusion)"
    }
    if ($i % 12 -eq 0) { Write-Host 'Full baseline is still building...' }
    Start-Sleep -Seconds 5
  }
  throw 'Timed out waiting for the full ARM64 baseline.'
}

function Download-Artifact($Info,[string]$Dir) {
  New-Item -ItemType Directory -Force -Path $Dir | Out-Null
    $downloadRunId = Get-RunId $Info.run
  & $gh.Source run download $downloadRunId --repo $Repo --name $Info.artifact --dir $Dir
  if ($LASTEXITCODE -ne 0) { throw "Artifact download failed: $($Info.artifact)" }
  $exe = Get-ChildItem $Dir -Filter MiaoDesk.exe -File -Recurse | Select-Object -First 1
  if (-not $exe) { throw "Artifact has no MiaoDesk.exe: $($Info.artifact)" }
  $exe.Directory.FullName
}

function Stop-Product {
  foreach ($n in @('MiaoDesk','MiaoDeskWallpaper','MiaoDeskHarness')) {
    Get-Process $n -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
  }
}

function Ensure-Goz([string]$SourceRoot) {
  $src = Join-Path $SourceRoot 'Goz'
  if (-not (Test-Path (Join-Path $src 'goz.exe') -PathType Leaf)) { throw 'Full baseline has no Goz runtime.' }

  $existing = Get-CimInstance Win32_Service -Filter "Name='goz'" -ErrorAction SilentlyContinue
  $dest = Join-Path $RunRoot 'Goz'
  if ($existing) {
    New-Item -ItemType Directory -Force -Path $dest | Out-Null
    try {
      Copy-Item (Join-Path $src '*') $dest -Recurse -Force
      & (Join-Path $dest 'goz.exe') --status *> $null
      if ($LASTEXITCODE -eq 0) { Pass 'Existing goz service is usable'; return }
    } catch {}
  }

  $helper = Join-Path $PSScriptRoot 'update-goz-dev-service.ps1'
  if (-not (Test-Path $helper -PathType Leaf)) { throw "Missing helper: $helper" }
  $hostExe = (Get-Process -Id $PID).Path
  $argLine = "-NoProfile -ExecutionPolicy Bypass -File `"$helper`" -SourceGoz `"$src`" -DestinationGoz `"$dest`""
  Write-Host 'A one-time UAC prompt may appear to install/update the goz file-search service.' -ForegroundColor Yellow
  $p = Start-Process $hostExe -Verb RunAs -ArgumentList $argLine -Wait -PassThru
  if ($p.ExitCode -ne 0) { throw "Goz service refresh failed: $($p.ExitCode)" }
  & (Join-Path $dest 'goz.exe') --status *> $null
  if ($LASTEXITCODE -ne 0) { throw 'Goz service is not queryable after refresh.' }
  Pass 'Goz service ready'
}

function Apply-Full([string]$SourceRoot) {
  Step 'Apply complete ARM64 baseline'
  Stop-Product
  New-Item -ItemType Directory -Force -Path $RunRoot | Out-Null
  foreach ($d in @('Runtime','AI','Assets','Config','Wallpapers','Widgets','skills')) {
    Remove-Item (Join-Path $RunRoot $d) -Recurse -Force -ErrorAction SilentlyContinue
  }
  foreach ($f in @('MiaoDesk.exe','MiaoDeskWallpaper.exe','MiaoDeskHarness.exe','LICENSE','THIRD-PARTY-NOTICES.md')) {
    Remove-Item (Join-Path $RunRoot $f) -Force -ErrorAction SilentlyContinue
  }
  robocopy $SourceRoot $RunRoot /E /XD (Join-Path $SourceRoot 'Goz') /R:2 /W:1 /NFL /NDL /NJH /NJS /NP | Out-Null
  if ($LASTEXITCODE -ge 8) { throw "Full baseline copy failed: $LASTEXITCODE" }
  Ensure-Goz $SourceRoot
}

function Apply-Delta([string]$SourceRoot) {
  Step 'Apply fast ARM64 overlay'
  Stop-Product
  New-Item -ItemType Directory -Force -Path $RunRoot | Out-Null
  robocopy $SourceRoot $RunRoot /E /R:2 /W:1 /NFL /NDL /NJH /NJS /NP | Out-Null
  if ($LASTEXITCODE -ge 8) { throw "Delta overlay copy failed: $LASTEXITCODE" }
}

function Verify-And-Launch {
  Step 'Fast self-tests'
  foreach ($e in @('MiaoDesk.exe','MiaoDeskWallpaper.exe','MiaoDeskHarness.exe')) {
    $p = Start-Process (Join-Path $RunRoot $e) -ArgumentList '--self-test' -Wait -PassThru
    if ($p.ExitCode -ne 0) { throw "$e --self-test failed: $($p.ExitCode)" }
    Pass "$e --self-test"
  }
  $goz = Join-Path $RunRoot 'Goz\goz.exe'
  if (-not (Test-Path $goz -PathType Leaf)) { throw 'Goz client is missing from dev runtime.' }
  & $goz --status *> $null
  if ($LASTEXITCODE -ne 0) { throw 'Goz file-search service is not queryable.' }
  Pass 'Goz file search service'
  if (-not $NoLaunch) {
    Start-Process (Join-Path $RunRoot 'MiaoDesk.exe') -WorkingDirectory $RunRoot
    Pass 'MiaoDesk launched'
  }
}

try {
  $arch = [Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()
  if ($arch -ne 'Arm64') { throw "Requires Windows ARM64; detected $arch" }
  $gh = Get-Command gh.exe -ErrorAction SilentlyContinue
  if (-not $gh) { throw 'GitHub CLI (gh) is required. Install it once and run: gh auth login' }

  Step 'Sync main'
  git pull --ff-only
  if ($LASTEXITCODE -ne 0) { throw 'git pull failed' }
  $sha = (& git rev-parse HEAD).Trim()
  $baselineKey = Get-BaselineKey 'HEAD'

  $state = $null
  if (Test-Path $StatePath -PathType Leaf) {
    try { $state = Get-Content $StatePath -Raw | ConvertFrom-Json } catch {}
  }

  if ($state -and $state.installedSha -eq $sha -and (Test-Path (Join-Path $RunRoot 'MiaoDesk.exe') -PathType Leaf)) {
    Pass "Already on current commit: $sha"
    Verify-And-Launch
    exit 0
  }

  $needFull = (-not $state) -or
              (-not (Test-Path (Join-Path $RunRoot 'Runtime\Node\node.exe') -PathType Leaf)) -or
              (-not (Test-Path (Join-Path $RunRoot 'AI\node_modules\pi\dist\cli.js') -PathType Leaf)) -or
              ([string]$state.baselineKey -ne $baselineKey)

  if (-not $needFull -and $state.installedSha) {
    Ensure-Commit ([string]$state.installedSha)
    & git merge-base --is-ancestor $state.installedSha HEAD 2>$null
    if ($LASTEXITCODE -ne 0) {
      $needFull = $true
    } else {
      foreach ($line in @(& git diff --name-status $state.installedSha HEAD)) {
        $p = "$line" -split "`t"
        if ($p.Count -ge 2 -and $p[0] -match '^[DR]' -and
            ($p[-1] -match '^(assets/(app|wallpapers|widgets)/|config/|skills/)')) {
          $needFull = $true
          break
        }
      }
    }
  }

  $temp = Join-Path $env:TEMP ("MiaoDeskQuick-" + [Guid]::NewGuid().ToString('N'))
  New-Item -ItemType Directory -Force -Path $temp | Out-Null
  try {
    if ($needFull) {
      $info = Find-CompatibleFullRun $baselineKey $sha
      $source = Download-Artifact $info (Join-Path $temp 'full')
      Apply-Full $source
      $mode = 'full'
      $runId = Get-RunId $info.run
    } else {
      $info = Wait-FastRun $sha
      $source = Download-Artifact $info (Join-Path $temp 'delta')
      Apply-Delta $source
      $mode = 'delta'
      $runId = $info.run.databaseId
    }

    [ordered]@{
      schema = 2
      installedSha = $sha
      baselineKey = $baselineKey
      mode = $mode
      runId = $runId
      updatedUtc = [DateTime]::UtcNow.ToString('o')
    } | ConvertTo-Json | Set-Content $StatePath -Encoding utf8

    Verify-And-Launch
    Write-Host ""
    Write-Host 'ARM64 QUICK TEST READY' -ForegroundColor Green
    Write-Host "Commit: $sha"
    Write-Host "Mode:   $mode"
    Write-Host "Run:    $RunRoot"
  } finally {
    Remove-Item $temp -Recurse -Force -ErrorAction SilentlyContinue
  }
} catch {
  Write-Host ""
  Write-Host 'ARM64 QUICK TEST FAILED' -ForegroundColor Red
  Write-Host $_.Exception.Message -ForegroundColor Red
  Read-Host 'Press Enter to close' | Out-Null
  exit 1
}
