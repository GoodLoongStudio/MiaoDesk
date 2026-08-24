param(
    [string]$Repo = "GoodLoongStudio/TuringDesk"
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

$ArtifactName = "TuringDesk-Native-Search-ARM64"
$Workflow = "native-search-windows.yml"
$DeployDir = Join-Path $env:LOCALAPPDATA "TuringDesk\NativeTest"
$DeployParent = Split-Path $DeployDir -Parent

function Step([string]$Text) { Write-Host "`n==> $Text" -ForegroundColor Cyan }

function Invoke-GhJson([string[]]$Arguments) {
    for ($attempt = 1; $attempt -le 5; $attempt++) {
        $output = @(& gh @Arguments 2>$null)
        $code = $LASTEXITCODE
        if ($code -eq 0) {
            try {
                $text = [string]($output -join "`n")
                if ([string]::IsNullOrWhiteSpace($text)) { return $null }
                return ($text | ConvertFrom-Json)
            } catch { }
        }
        if ($attempt -lt 5) {
            Write-Host ("GitHub query failed ({0}/5); retrying..." -f $attempt) -ForegroundColor Yellow
            Start-Sleep -Seconds ([Math]::Min($attempt * 2, 6))
        }
    }
    throw "GitHub query failed after retries"
}

function Invoke-GhText([string[]]$Arguments) {
    for ($attempt = 1; $attempt -le 5; $attempt++) {
        $output = @(& gh @Arguments 2>$null)
        $code = $LASTEXITCODE
        $text = [string]($output -join "`n")
        if ($code -eq 0 -and -not [string]::IsNullOrWhiteSpace($text)) { return $text.Trim() }
        if ($attempt -lt 5) {
            Write-Host ("GitHub query failed ({0}/5); retrying..." -f $attempt) -ForegroundColor Yellow
            Start-Sleep -Seconds ([Math]::Min($attempt * 2, 6))
        }
    }
    throw "GitHub query failed after retries"
}

function Test-BuildRelevantPath([string]$Path) {
    if ($Path -eq "CMakeLists.txt") { return $true }
    if ($Path -like "src/native/*") { return $true }
    if ($Path -like "runtime/arm64/*") { return $true }
    if ($Path -like "scripts/*arm64*.ps1") { return $true }
    if ($Path -eq "scripts/verify-l3-runtime-contract.ps1") { return $true }
    if ($Path -eq "scripts/verify-runtime-log-paths.ps1") { return $true }
    if ($Path -eq ".github/workflows/native-search-windows.yml") { return $true }
    if ($Path -eq ".github/workflows/vendor-arm64-runtime.yml") { return $true }
    return $false
}

function Resolve-ValidatedRun([string]$MainSha) {
    Step "Finding latest validated TuringDesk ARM64 package"
    $runs = @(Invoke-GhJson @(
        "run", "list", "--repo", $Repo, "--workflow", $Workflow, "--branch", "main", "--limit", "30",
        "--json", "databaseId,headSha,status,conclusion,createdAt"
    ))
    $run = $runs | Where-Object { $_.status -eq "completed" -and $_.conclusion -eq "success" } |
        Sort-Object createdAt -Descending | Select-Object -First 1
    if (-not $run) { throw "No validated TuringDesk ARM64 build is available. The updater will not install an unverified package." }

    $buildSha = [string]$run.headSha
    if ($buildSha -ne $MainSha) {
        $compare = Invoke-GhJson @("api", "repos/$Repo/compare/$buildSha...$MainSha")
        if (-not $compare) { throw "Unable to compare the latest validated build with current main." }
        if ([string]$compare.status -notin @("ahead", "identical")) {
            throw "Current main is not a clean forward descendant of the latest validated ARM64 build."
        }
        $relevant = @($compare.files | Where-Object { Test-BuildRelevantPath ([string]$_.filename) })
        if ($relevant.Count -gt 0) {
            $names = ($relevant | ForEach-Object { [string]$_.filename }) -join ", "
            throw ("Current main contains TuringDesk build/runtime changes that have not passed ARM64 validation: {0}" -f $names)
        }
        Write-Host "The latest validated package is still current; newer main changes are non-runtime changes." -ForegroundColor DarkGray
    }

    [pscustomobject]@{ RunId = [long]$run.databaseId; BuildSha = $buildSha }
}

function Download-Artifact([long]$RunId, [string]$Destination) {
    Step ("Downloading validated TuringDesk ARM64 binaries from run {0}" -f $RunId)
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    & gh run download $RunId --repo $Repo --name $ArtifactName --dir $Destination | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "Unable to download the validated TuringDesk ARM64 artifact." }
}

function Materialize-Runtime([string]$Destination, [string]$BuildSha) {
    Step "Materializing the matching TuringDesk RuntimeBundle"
    if (-not (Get-Command git -ErrorAction SilentlyContinue)) { throw "Git was not found in PATH." }

    $runtimeRepo = Join-Path $env:TEMP ("TuringDesk-RuntimeSource-" + [guid]::NewGuid().ToString("N"))
    try {
        & git clone --filter=blob:none --no-checkout "https://github.com/$Repo.git" $runtimeRepo | Out-Host
        if ($LASTEXITCODE -ne 0) { throw "Unable to fetch TuringDesk RuntimeBundle source." }
        & git -C $runtimeRepo sparse-checkout init --cone | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "Unable to initialize sparse checkout." }
        & git -C $runtimeRepo sparse-checkout set runtime/arm64 scripts | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "Unable to select RuntimeBundle files." }
        & git -C $runtimeRepo checkout $BuildSha | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "Unable to checkout the validated RuntimeBundle revision $BuildSha." }
        $runtimeSha = (& git -C $runtimeRepo rev-parse HEAD).Trim()
        if ($LASTEXITCODE -ne 0 -or $runtimeSha -ne $BuildSha) {
            throw "RuntimeBundle revision mismatch. Update aborted before touching the installed TuringDesk package."
        }

        $prepare = Join-Path $runtimeRepo "scripts\prepare-third-party-runtime-arm64.ps1"
        if (-not (Test-Path $prepare -PathType Leaf)) { throw "Runtime preparation script is missing." }
        & powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File $prepare -DeployDir $Destination -SkipGozServiceInstall | Out-Host
        if ($LASTEXITCODE -ne 0) { throw "TuringDesk RuntimeBundle preparation failed." }
    }
    finally { Remove-Item $runtimeRepo -Recurse -Force -ErrorAction SilentlyContinue }
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
    Step "Running full TuringDesk package self-tests before installation"
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
}

function Stop-DeployedProcesses {
    Step "Stopping currently installed TuringDesk processes"
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
        throw "TuringDesk file index service executable is missing: $Exe"
    }
    try {
        $process = Start-Process -FilePath $Exe -ArgumentList $Arguments -Verb RunAs -Wait -PassThru
        if (-not $process -or $process.ExitCode -ne 0) {
            if (-not $IgnoreFailure) { throw ("TuringDesk file index service operation failed: {0}" -f $Arguments) }
        }
    } catch { if (-not $IgnoreFailure) { throw } }
}

function Probe([string]$Exe, [string[]]$Arguments) {
    $out = Join-Path $env:TEMP ('td-update-probe-o-' + [guid]::NewGuid().ToString('N'))
    $err = Join-Path $env:TEMP ('td-update-probe-e-' + [guid]::NewGuid().ToString('N'))
    try {
        $p = Start-Process -FilePath $Exe -ArgumentList $Arguments -Wait -PassThru -NoNewWindow -RedirectStandardOutput $out -RedirectStandardError $err -ErrorAction SilentlyContinue
        if (-not $p) { return -1 }
        return [int]$p.ExitCode
    } catch { return -1 }
    finally { Remove-Item $out,$err -Force -ErrorAction SilentlyContinue }
}

function Wait-IndexReady([string]$IndexExe) {
    Assert-File $IndexExe "installed file index client"
    for ($i = 0; $i -lt 120; $i++) {
        if ((Probe $IndexExe @("--status")) -eq 0) { return }
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

try {
    if (-not (Get-Command gh -ErrorAction SilentlyContinue)) { throw "GitHub CLI (gh) was not found in PATH." }

    Step "Checking GitHub authentication"
    & gh auth status | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "GitHub CLI is not authenticated. Run: gh auth login" }

    Step "Resolving current TuringDesk main"
    $mainSha = Invoke-GhText @("api", "repos/$Repo/commits/main", "--jq", ".sha")
    Write-Host ("main: {0}" -f $mainSha) -ForegroundColor DarkGray
    $validated = Resolve-ValidatedRun $mainSha
    Write-Host ("validated build: {0} / run {1}" -f $validated.BuildSha, $validated.RunId) -ForegroundColor Green

    $work = Join-Path $env:TEMP ("TuringDesk-Updater-" + [guid]::NewGuid().ToString("N"))
    $artifact = Join-Path $work "artifact"
    $next = Join-Path $DeployParent ("NativeTest.next-" + [guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Force -Path $work, $next, $DeployParent | Out-Null

    try {
        Download-Artifact $validated.RunId $artifact
        Materialize-Runtime $next $validated.BuildSha
        Copy-Item (Join-Path $artifact "*") $next -Recurse -Force
        Set-Content (Join-Path $next ".installed-build-sha") -Value $validated.BuildSha -Encoding ASCII

        # All staged tests happen before the current installation is touched.
        Test-StagedPackage $next

        $oldIndexService = Join-Path $DeployDir "Goz\gozd.exe"
        Stop-DeployedProcesses
        Invoke-ElevatedIndexService $oldIndexService "uninstall" -IgnoreFailure
        Stop-DeployedProcesses

        Step "Installing validated TuringDesk ARM64 package"
        if (Test-Path $DeployDir) {
            Remove-Item -LiteralPath $DeployDir -Recurse -Force -ErrorAction Stop
        }
        if (Test-Path $DeployDir) { throw "Existing TuringDesk installation directory could not be removed: $DeployDir" }
        if (-not (Test-Path $next -PathType Container)) { throw "Staged TuringDesk package disappeared before install: $next" }
        Move-Item -LiteralPath $next -Destination $DeployDir -ErrorAction Stop
        if (-not (Test-Path $DeployDir -PathType Container)) { throw "Installed TuringDesk package directory is missing after move: $DeployDir" }

        $newIndexService = Join-Path $DeployDir "Goz\gozd.exe"
        $newIndexClient = Join-Path $DeployDir "Goz\goz.exe"
        Assert-File $newIndexService "installed file index service"
        Assert-File $newIndexClient "installed file index client"
        Assert-File (Join-Path $DeployDir "Pi\node_modules\@earendil-works\pi-coding-agent\dist\cli.js") "installed agent runtime"
        Invoke-ElevatedIndexService $newIndexService "install"
        Wait-IndexReady $newIndexClient

        Step "Running installed TuringDesk self-tests"
        Test-Binary (Join-Path $DeployDir "TuringDesk.exe") "TuringDesk"
        Test-Binary (Join-Path $DeployDir "TuringDeskWallpaper.exe") "TuringDesk Wallpaper"
        Test-Binary (Join-Path $DeployDir "TuringDeskHarness.exe") "TuringDesk Advanced Workbench"

        Step "Running advanced workbench smoke test"
        Test-Binary (Join-Path $DeployDir "TuringDeskHarness.exe") "TuringDesk Advanced Workbench smoke" @("--harness-smoke-test")

        Step "Starting TuringDesk"
        $wallpaper = Join-Path $DeployDir "TuringDeskWallpaper.exe"
        if (Should-ShowWallpaperSettings) { Start-Process -FilePath $wallpaper -ArgumentList "--settings" }
        else { Start-Process -FilePath $wallpaper }
        Start-Process -FilePath (Join-Path $DeployDir "TuringDesk.exe")

        Write-Host "`n图灵智能桌面更新完成。" -ForegroundColor Green
        Write-Host ("已安装验证版本：{0}" -f $validated.BuildSha) -ForegroundColor Green
        Write-Host ("GitHub Actions：{0}" -f $validated.RunId) -ForegroundColor DarkGray
        Write-Host ("安装目录：{0}" -f $DeployDir) -ForegroundColor DarkGray
    }
    finally {
        Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
        if (Test-Path $next) { Remove-Item $next -Recurse -Force -ErrorAction SilentlyContinue }
    }
}
catch {
    Write-Host "`n图灵智能桌面更新失败。" -ForegroundColor Red
    Write-Host $_.Exception.Message -ForegroundColor Red
    Write-Host "现有安装在 staging 自测通过前不会被修改；若失败发生在替换阶段，请重新运行更新脚本。" -ForegroundColor Yellow
    exit 1
}
