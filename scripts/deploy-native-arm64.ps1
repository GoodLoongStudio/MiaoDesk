param(
    [string]$Repo = "GoodLoongStudio/TuringDesk"
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

$RepoRoot = Split-Path $PSScriptRoot -Parent
$DeployDir = Join-Path $env:LOCALAPPDATA "TuringDesk\NativeTest"
$DeployParent = Split-Path $DeployDir -Parent
$ArtifactName = "TuringDesk-Native-Search-ARM64"
$Workflow = "native-search-windows.yml"

function Step([string]$Text) { Write-Host "`n==> $Text" -ForegroundColor Cyan }

function Assert-TuringDeskRuntimeContract {
    $guard = Join-Path $RepoRoot "scripts\verify-l3-runtime-contract.ps1"
    if (-not (Test-Path $guard -PathType Leaf)) { throw "Missing TuringDesk AI runtime guard: $guard" }
    Step "Verifying TuringDesk AI runtime contract"
    & powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File $guard | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "TuringDesk AI runtime contract failed" }
}

function Assert-File([string]$Path, [string]$Label) {
    if (-not (Test-Path $Path -PathType Leaf)) { throw ("Package is missing {0}: {1}" -f $Label, $Path) }
}

function Test-Binary([string]$Exe, [string]$Name, [string[]]$Arguments = @("--self-test")) {
    Write-Host ("Testing {0}..." -f $Name) -ForegroundColor DarkGray
    $process = Start-Process -FilePath $Exe -ArgumentList $Arguments -Wait -PassThru -NoNewWindow
    if ($process.ExitCode -ne 0) { throw ("{0} test failed with exit code {1}" -f $Name, $process.ExitCode) }
}

function Test-StagedPackage([string]$Root) {
    Step "Running full TuringDesk package self-tests before deployment"
    $search = Join-Path $Root "TuringDesk.exe"
    $wallpaper = Join-Path $Root "TuringDeskWallpaper.exe"
    $workbench = Join-Path $Root "TuringDeskHarness.exe"
    $node = Join-Path $Root "Runtime\Node\node.exe"
    $workbenchCli = Join-Path $Root "Runtime\Node\node_modules\@deepseek-ai\dsh\lib\bin.js"
    $agentCli = Join-Path $Root "Pi\node_modules\@earendil-works\pi-coding-agent\dist\cli.js"
    $goz = Join-Path $Root "Goz\goz.exe"
    $gozd = Join-Path $Root "Goz\gozd.exe"

    Assert-File $search "TuringDesk.exe"
    Assert-File $wallpaper "TuringDeskWallpaper.exe"
    Assert-File $workbench "TuringDeskHarness.exe"
    Assert-File $node "bundled AI runtime"
    Assert-File $workbenchCli "advanced workbench runtime"
    Assert-File $agentCli "agent runtime"
    Assert-File $goz "file index client"
    Assert-File $gozd "file index service"

    Test-Binary $search "TuringDesk"
    Test-Binary $wallpaper "TuringDesk Wallpaper"
    Test-Binary $workbench "TuringDesk Advanced Workbench"
    Test-Binary $node "TuringDesk Agent Runtime" @($agentCli, "--version")
    Test-Binary $workbench "TuringDesk Advanced Workbench smoke" @("--harness-smoke-test")
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
    Step "Waiting for TuringDesk ARM64 validation run $RunId"
    & gh run watch $RunId --repo $Repo --exit-status | Out-Host
    if ($LASTEXITCODE -ne 0) {
        & gh run view $RunId --repo $Repo --log-failed | Out-Host
        throw "TuringDesk ARM64 validation failed (run $RunId)"
    }
    return $RunId
}

function Start-And-WaitForRun([string]$Sha) {
    $before = @((Get-RunsForCommit -Sha $Sha) | ForEach-Object { [long]$_.databaseId })
    Step "Starting ARM64 validation"
    & gh workflow run $Workflow --repo $Repo --ref main | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "Unable to start ARM64 validation workflow" }
    $run = $null
    for ($i = 0; $i -lt 45; $i++) {
        Start-Sleep -Seconds 2
        $run = (Get-RunsForCommit -Sha $Sha) |
            Where-Object { ([long]$_.databaseId -notin $before) -and $_.event -eq "workflow_dispatch" } |
            Sort-Object createdAt -Descending | Select-Object -First 1
        if ($run) { break }
    }
    if (-not $run) { throw "ARM64 validation was started but its run could not be found" }
    return (Wait-ForRun -RunId ([long]$run.databaseId))
}

function Resolve-Run([string]$Sha) {
    $runs = Get-RunsForCommit -Sha $Sha
    $successful = $runs | Where-Object { $_.status -eq "completed" -and $_.conclusion -eq "success" } |
        Sort-Object createdAt -Descending | Select-Object -First 1
    if ($successful) {
        Step "Found validated ARM64 build for current main: run $($successful.databaseId)"
        return [long]$successful.databaseId
    }
    $running = $runs | Where-Object { $_.status -ne "completed" } | Sort-Object createdAt -Descending | Select-Object -First 1
    if ($running) { return (Wait-ForRun -RunId ([long]$running.databaseId)) }
    return (Start-And-WaitForRun -Sha $Sha)
}

function Download-Artifact([long]$RunId, [string]$Destination) {
    Step "Downloading validated TuringDesk ARM64 binaries"
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    & gh run download $RunId --repo $Repo --name $ArtifactName --dir $Destination | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "Unable to download TuringDesk ARM64 artifact" }
}

function Materialize-Runtime([string]$Destination, [string]$ExpectedSha) {
    Step "Staging pinned TuringDesk RuntimeBundle"
    if (-not (Get-Command git -ErrorAction SilentlyContinue)) { throw "Git was not found in PATH." }

    $runtimeRepo = Join-Path $env:TEMP ("TuringDesk-RuntimeSource-" + [guid]::NewGuid().ToString("N"))
    try {
        & git clone --filter=blob:none --no-checkout --depth 1 --branch main "https://github.com/$Repo.git" $runtimeRepo | Out-Host
        if ($LASTEXITCODE -ne 0) { throw "Unable to fetch TuringDesk RuntimeBundle source." }
        & git -C $runtimeRepo sparse-checkout init --cone | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "Unable to initialize sparse checkout." }
        & git -C $runtimeRepo sparse-checkout set runtime/arm64 scripts | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "Unable to select RuntimeBundle files." }
        & git -C $runtimeRepo checkout main | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "Unable to checkout RuntimeBundle files." }
        $runtimeSha = (& git -C $runtimeRepo rev-parse HEAD).Trim()
        if ($LASTEXITCODE -ne 0 -or $runtimeSha -ne $ExpectedSha) {
            throw "main changed while deploying. Re-run deployment so binaries and RuntimeBundle use the same commit."
        }

        $prepare = Join-Path $runtimeRepo "scripts\prepare-third-party-runtime-arm64.ps1"
        if (-not (Test-Path $prepare -PathType Leaf)) { throw "Runtime preparation script is missing." }
        & powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File $prepare -DeployDir $Destination -SkipGozServiceInstall | Out-Host
        if ($LASTEXITCODE -ne 0) { throw "TuringDesk RuntimeBundle preparation failed." }
    }
    finally { Remove-Item $runtimeRepo -Recurse -Force -ErrorAction SilentlyContinue }
}

function Stop-DeployedProcesses {
    Step "Stopping currently deployed TuringDesk processes"
    $names = @("TuringDesk.exe", "TuringDeskWallpaper.exe", "TuringDeskHarness.exe", "node.exe", "goz.exe", "gozd.exe")
    try {
        $deployRoot = [IO.Path]::GetFullPath($DeployDir).TrimEnd("\") + "\"
        foreach ($process in @(Get-CimInstance Win32_Process -ErrorAction Stop)) {
            if ($names -notcontains [string]$process.Name) { continue }
            $exe = [string]$process.ExecutablePath
            if (-not $exe) { continue }
            try {
                $full = [IO.Path]::GetFullPath($exe)
                if ($full.StartsWith($deployRoot, [StringComparison]::OrdinalIgnoreCase)) {
                    & taskkill.exe /PID $process.ProcessId /T /F 2>$null | Out-Null
                }
            } catch { }
        }
    } catch { }
    foreach ($name in @("TuringDesk", "TuringDeskWallpaper", "TuringDeskHarness")) {
        Get-Process -Name $name -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    }
    Start-Sleep -Milliseconds 750
}

function Invoke-ElevatedIndexService([string]$Exe, [string]$Arguments, [switch]$IgnoreFailure) {
    if (-not (Test-Path $Exe -PathType Leaf)) {
        if ($IgnoreFailure) { return }
        throw "File index service executable is missing: $Exe"
    }
    try {
        $process = Start-Process -FilePath $Exe -ArgumentList $Arguments -Verb RunAs -Wait -PassThru
        if (-not $process -or $process.ExitCode -ne 0) {
            if (-not $IgnoreFailure) { throw ("File index service operation failed: {0}" -f $Arguments) }
        }
    } catch { if (-not $IgnoreFailure) { throw } }
}

function Probe([string]$Exe, [string[]]$Arguments) {
    $out = Join-Path $env:TEMP ('td-deploy-probe-o-' + [guid]::NewGuid().ToString('N'))
    $err = Join-Path $env:TEMP ('td-deploy-probe-e-' + [guid]::NewGuid().ToString('N'))
    try {
        $p = Start-Process -FilePath $Exe -ArgumentList $Arguments -Wait -PassThru -NoNewWindow -RedirectStandardOutput $out -RedirectStandardError $err -ErrorAction SilentlyContinue
        if (-not $p) { return -1 }
        return [int]$p.ExitCode
    } catch { return -1 }
    finally { Remove-Item $out,$err -Force -ErrorAction SilentlyContinue }
}

function Wait-IndexReady([string]$Exe) {
    Assert-File $Exe "installed file index client"
    for ($i = 0; $i -lt 120; $i++) {
        if ((Probe $Exe @("--status")) -eq 0) { return }
        Start-Sleep -Milliseconds 500
    }
    throw "TuringDesk file index service did not become reachable after installation."
}

function Should-ShowWallpaperSettings {
    $config = Join-Path $env:LOCALAPPDATA "TuringDesk\wallpaper.ini"
    if (-not (Test-Path $config -PathType Leaf)) { return $true }
    try { return -not [bool](Select-String -Path $config -Pattern "^Version=3$" -ErrorAction Stop) }
    catch { return $true }
}

Assert-TuringDeskRuntimeContract
if (-not (Get-Command gh -ErrorAction SilentlyContinue)) { throw "GitHub CLI (gh) was not found in PATH" }
Step "Checking GitHub authentication"
& gh auth status 2>$null | Out-Host
if ($LASTEXITCODE -ne 0) { throw "GitHub CLI is not authenticated. Run: gh auth login" }

Step "Resolving current TuringDesk main"
$mainSha = Get-MainSha
Write-Host "main: $mainSha" -ForegroundColor DarkGray
$runId = [long](Resolve-Run -Sha $mainSha)

$work = Join-Path $env:TEMP ("TuringDesk-Deploy-" + [guid]::NewGuid().ToString("N"))
$artifact = Join-Path $work "artifact"
$next = Join-Path $DeployParent ("NativeTest.next-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force -Path $work, $next, $DeployParent | Out-Null

try {
    Download-Artifact -RunId $runId -Destination $artifact
    Materialize-Runtime -Destination $next -ExpectedSha $mainSha
    Copy-Item (Join-Path $artifact "*") $next -Recurse -Force
    Set-Content (Join-Path $next ".installed-build-sha") -Value $mainSha -Encoding ASCII

    Test-StagedPackage $next

    $oldIndexService = Join-Path $DeployDir "Goz\gozd.exe"
    Stop-DeployedProcesses
    Invoke-ElevatedIndexService $oldIndexService "uninstall" -IgnoreFailure
    Stop-DeployedProcesses

    Step "Deploying validated TuringDesk ARM64 package"
    if (Test-Path $DeployDir) { Remove-Item -LiteralPath $DeployDir -Recurse -Force -ErrorAction Stop }
    if (Test-Path $DeployDir) { throw "Existing TuringDesk deployment could not be removed: $DeployDir" }
    Move-Item -LiteralPath $next -Destination $DeployDir -ErrorAction Stop

    $newIndexService = Join-Path $DeployDir "Goz\gozd.exe"
    $newIndexClient = Join-Path $DeployDir "Goz\goz.exe"
    Invoke-ElevatedIndexService $newIndexService "install"
    Wait-IndexReady $newIndexClient

    Step "Running installed TuringDesk self-tests"
    Test-Binary (Join-Path $DeployDir "TuringDesk.exe") "TuringDesk"
    Test-Binary (Join-Path $DeployDir "TuringDeskWallpaper.exe") "TuringDesk Wallpaper"
    Test-Binary (Join-Path $DeployDir "TuringDeskHarness.exe") "TuringDesk Advanced Workbench"

    Step "Starting TuringDesk"
    $wallpaper = Join-Path $DeployDir "TuringDeskWallpaper.exe"
    if (Should-ShowWallpaperSettings) { Start-Process -FilePath $wallpaper -ArgumentList "--settings" }
    else { Start-Process -FilePath $wallpaper }
    Start-Process -FilePath (Join-Path $DeployDir "TuringDesk.exe")

    Write-Host "`n图灵智能桌面部署完成。按 Alt+Space 打开。" -ForegroundColor Green
    Write-Host ("已部署版本：{0}" -f $mainSha) -ForegroundColor DarkGray
    Write-Host ("安装目录：{0}" -f $DeployDir) -ForegroundColor DarkGray
}
finally {
    Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
    if (Test-Path $next) { Remove-Item $next -Recurse -Force -ErrorAction SilentlyContinue }
}
