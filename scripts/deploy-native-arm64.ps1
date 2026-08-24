param(
    [string]$Repo = "GoodLoongStudio/TuringDesk"
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

$RepoRoot = Split-Path $PSScriptRoot -Parent
$DeployDir = Join-Path $env:LOCALAPPDATA "TuringDesk\NativeTest"
$ArtifactName = "TuringDesk-Native-Search-ARM64"
$Workflow = "native-search-windows.yml"
$ExeName = "TuringDesk.exe"
$WallpaperExeName = "TuringDeskWallpaper.exe"
$HarnessExeName = "TuringDeskHarness.exe"

function Step([string]$Text) { Write-Host "`n==> $Text" -ForegroundColor Cyan }

function Assert-L3RuntimeContract {
    $guard = Join-Path $RepoRoot "scripts\verify-l3-runtime-contract.ps1"
    if (-not (Test-Path $guard -PathType Leaf)) { throw "Missing L3 runtime contract guard: $guard" }
    Step "Verifying Pi-first L3 runtime contract"
    & powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File $guard | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "L3 Pi-first runtime contract failed" }
}

function Assert-DeployedPiRuntime {
    $node = Join-Path $DeployDir "Runtime\Node\node.exe"
    $pi = Join-Path $DeployDir "Pi\node_modules\@earendil-works\pi-coding-agent\dist\cli.js"
    if (-not (Test-Path $node -PathType Leaf)) { throw "Local runtime is missing bundled Node: $node" }
    if (-not (Test-Path $pi -PathType Leaf)) { throw "Local runtime is missing Pi Agent: $pi" }
    & $node $pi --version | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "Local Pi Agent failed --version" }
}

function Stop-DeployedInstance {
    Step "Stopping previous TuringDesk processes"
    foreach ($processName in @("TuringDesk", "TuringDeskWallpaper", "TuringDeskHarness")) {
        foreach ($process in @(Get-Process -Name $processName -ErrorAction SilentlyContinue)) {
            try { Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue } catch { }
        }
    }
    try {
        $runtimeRoot = [System.IO.Path]::GetFullPath($DeployDir).TrimEnd("\") + "\"
        foreach ($process in @(Get-CimInstance Win32_Process -Filter "Name='node.exe'" -ErrorAction Stop)) {
            $exe = [string]$process.ExecutablePath
            if (-not $exe) { continue }
            $full = [System.IO.Path]::GetFullPath($exe)
            if ($full.StartsWith($runtimeRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
                & taskkill.exe /PID $process.ProcessId /T /F 2>$null | Out-Null
            }
        }
    } catch { }
    Start-Sleep -Milliseconds 500
}

function Copy-WithRetry([string]$Source, [string]$Destination) {
    $lastError = $null
    for ($i = 1; $i -le 25; $i++) {
        try {
            Copy-Item $Source $Destination -Force -ErrorAction Stop
            return
        } catch {
            $lastError = $_
            Start-Sleep -Milliseconds 200
        }
    }
    throw "Unable to replace $Destination after retries: $($lastError.Exception.Message)"
}

function Test-Binary([string]$Exe, [string]$Name) {
    Step "Running $Name self-test"
    $process = Start-Process -FilePath $Exe -ArgumentList "--self-test" -Wait -PassThru
    if ($process.ExitCode -ne 0) { throw "$Name self-test failed with exit code $($process.ExitCode)" }
}

function Test-HarnessSmoke([string]$Exe) {
    Step "Running bundled DeepSeek Harness smoke test"
    $process = Start-Process -FilePath $Exe -ArgumentList "--harness-smoke-test" -Wait -PassThru
    if ($process.ExitCode -ne 0) {
        $desktop = [Environment]::GetFolderPath([Environment+SpecialFolder]::DesktopDirectory)
        $log = Join-Path $desktop "TuringDesk-Logs\harness.log"
        throw "Bundled DeepSeek Harness smoke test failed with exit code $($process.ExitCode). Log: $log"
    }
}

function Get-MainSha {
    for ($attempt = 1; $attempt -le 5; $attempt++) {
        $output = @(& gh api "repos/$Repo/commits/main" --jq ".sha" 2>$null)
        $code = $LASTEXITCODE
        $sha = [string]($output | Select-Object -First 1)
        if ($code -eq 0 -and -not [string]::IsNullOrWhiteSpace($sha)) { return $sha.Trim() }
        if ($attempt -lt 5) { Start-Sleep -Seconds ([Math]::Min(2 * $attempt, 6)) }
    }
    throw "Unable to resolve main commit SHA after retries"
}

function Get-RunsForCommit([string]$Sha) {
    for ($attempt = 1; $attempt -le 5; $attempt++) {
        $json = @(& gh run list --repo $Repo --workflow $Workflow --commit $Sha --limit 20 --json databaseId,headSha,status,conclusion,event,createdAt 2>$null)
        if ($LASTEXITCODE -eq 0) {
            try {
                if ($json.Count -eq 0) { return @() }
                return @(($json -join "`n") | ConvertFrom-Json)
            } catch { }
        }
        if ($attempt -lt 5) { Start-Sleep -Seconds ([Math]::Min(2 * $attempt, 6)) }
    }
    throw "Unable to query GitHub Actions runs after retries"
}

function Wait-ForRun([long]$RunId) {
    Step "Waiting for ARM64 GitHub Actions run $RunId"
    & gh run watch $RunId --repo $Repo --exit-status | Out-Host
    if ($LASTEXITCODE -ne 0) {
        & gh run view $RunId --repo $Repo --log-failed | Out-Host
        throw "ARM64 GitHub Actions build failed (run $RunId)"
    }
    return $RunId
}

function Start-And-WaitForRun([string]$Sha) {
    $before = @((Get-RunsForCommit -Sha $Sha) | ForEach-Object { [long]$_.databaseId })
    Step "Starting ARM64-only native validation"
    & gh workflow run $Workflow --repo $Repo --ref main | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "Unable to start ARM64 GitHub Actions workflow" }
    $run = $null
    for ($i = 0; $i -lt 45; $i++) {
        Start-Sleep -Seconds 2
        $run = (Get-RunsForCommit -Sha $Sha) |
            Where-Object { ([long]$_.databaseId -notin $before) -and $_.event -eq "workflow_dispatch" } |
            Sort-Object createdAt -Descending | Select-Object -First 1
        if ($run) { break }
    }
    if (-not $run) { throw "ARM64 workflow was started but its run could not be found" }
    return (Wait-ForRun -RunId ([long]$run.databaseId))
}

function Resolve-Run([string]$Sha) {
    $runs = Get-RunsForCommit -Sha $Sha
    $successful = $runs | Where-Object { $_.status -eq "completed" -and $_.conclusion -eq "success" } |
        Sort-Object createdAt -Descending | Select-Object -First 1
    if ($successful) {
        Step "Found successful ARM64 build for current main: run $($successful.databaseId)"
        return [long]$successful.databaseId
    }
    $running = $runs | Where-Object { $_.status -ne "completed" } | Sort-Object createdAt -Descending | Select-Object -First 1
    if ($running) { return (Wait-ForRun -RunId ([long]$running.databaseId)) }
    return (Start-And-WaitForRun -Sha $Sha)
}

function Download-Artifact([long]$RunId) {
    $temp = Join-Path $env:TEMP ("TuringDesk-ARM64-" + [guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Force -Path $temp | Out-Null
    Step "Downloading verified ARM64 artifact from TuringDesk run $RunId"
    & gh run download $RunId --repo $Repo --name $ArtifactName --dir $temp | Out-Host
    if ($LASTEXITCODE -ne 0) {
        Remove-Item $temp -Recurse -Force -ErrorAction SilentlyContinue
        throw "Unable to download ARM64 artifact"
    }

    $searchExe = Get-ChildItem -Path $temp -Filter $ExeName -Recurse | Select-Object -First 1
    $wallpaperExe = Get-ChildItem -Path $temp -Filter $WallpaperExeName -Recurse | Select-Object -First 1
    $harnessExe = Get-ChildItem -Path $temp -Filter $HarnessExeName -Recurse | Select-Object -First 1
    foreach ($required in @($searchExe, $wallpaperExe, $harnessExe)) {
        if (-not $required) {
            Remove-Item $temp -Recurse -Force -ErrorAction SilentlyContinue
            throw "ARM64 artifact is missing a required TuringDesk executable"
        }
    }
    return @{ Exe=$searchExe.FullName; WallpaperExe=$wallpaperExe.FullName; HarnessExe=$harnessExe.FullName; Temp=$temp }
}

function Should-ShowWallpaperSettings {
    $config = Join-Path $env:LOCALAPPDATA "TuringDesk\wallpaper.ini"
    if (-not (Test-Path $config)) { return $true }
    try { return -not [bool](Select-String -Path $config -Pattern '^Version=3$' -ErrorAction Stop) }
    catch { return $true }
}

Assert-L3RuntimeContract
if (-not (Get-Command gh -ErrorAction SilentlyContinue)) { throw "GitHub CLI (gh) was not found in PATH" }
Step "Checking GitHub CLI authentication"
& gh auth status 2>$null | Out-Host
if ($LASTEXITCODE -ne 0) { throw "GitHub CLI is not authenticated. Run: gh auth login" }

Assert-DeployedPiRuntime
$bundledNode = Join-Path $DeployDir "Runtime\Node\node.exe"
$bundledHarness = Join-Path $DeployDir "Runtime\Node\node_modules\@deepseek-ai\dsh\lib\bin.js"
if (-not (Test-Path $bundledHarness -PathType Leaf)) { throw "Bundled DeepSeek Harness is missing: $bundledHarness" }
Write-Host "Pi Agent mode: repository-vendored official package + bundled ARM64 Node + Windows PowerShell shell backend" -ForegroundColor Green
Write-Host "Bundled Node: $bundledNode" -ForegroundColor DarkGray
Write-Host "AI route: Pi Agent primary; Direct API fallback only" -ForegroundColor Green

Step "Resolving current main commit"
$mainSha = Get-MainSha
Write-Host "main: $mainSha" -ForegroundColor DarkGray
$runId = [long](Resolve-Run -Sha $mainSha)
$downloaded = Download-Artifact -RunId $runId
try {
    Test-Binary -Exe $downloaded.Exe -Name "Native Search"
    Test-Binary -Exe $downloaded.WallpaperExe -Name "Native Wallpaper"
    Test-Binary -Exe $downloaded.HarnessExe -Name "Native Harness shell"

    Step "Deploying ARM64 binaries to $DeployDir"
    Stop-DeployedInstance
    New-Item -ItemType Directory -Force -Path $DeployDir | Out-Null
    $deployedExe = Join-Path $DeployDir $ExeName
    $deployedWallpaper = Join-Path $DeployDir $WallpaperExeName
    $deployedHarness = Join-Path $DeployDir $HarnessExeName
    Copy-WithRetry -Source $downloaded.Exe -Destination $deployedExe
    Copy-WithRetry -Source $downloaded.WallpaperExe -Destination $deployedWallpaper
    Copy-WithRetry -Source $downloaded.HarnessExe -Destination $deployedHarness

    Assert-DeployedPiRuntime
    Test-Binary -Exe $deployedExe -Name "Deployed Search"
    Test-Binary -Exe $deployedWallpaper -Name "Deployed Wallpaper"
    Test-Binary -Exe $deployedHarness -Name "Deployed Harness shell"
    Test-HarnessSmoke -Exe $deployedHarness

    Step "Starting TuringDesk Wallpaper"
    if (Should-ShowWallpaperSettings) { Start-Process -FilePath $deployedWallpaper -ArgumentList "--settings" }
    else { Start-Process -FilePath $deployedWallpaper }
    Step "Starting TuringDesk Native Search"
    Start-Process $deployedExe

    Write-Host "`nDeployment complete. Press Alt+Space to open Search." -ForegroundColor Green
    Write-Host "AI: Pi Agent primary; Direct API is fallback only." -ForegroundColor Green
    Write-Host "DeepSeek Harness and Pi are running from the pinned RuntimeBundle; no npm install occurs on the user machine." -ForegroundColor Green
    Write-Host "Path: $DeployDir" -ForegroundColor DarkGray
}
finally {
    if ($downloaded -and $downloaded.Temp) { Remove-Item $downloaded.Temp -Recurse -Force -ErrorAction SilentlyContinue }
}
