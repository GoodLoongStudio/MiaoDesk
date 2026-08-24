param(
    [string]$Repo = "GoodLoongStudio/TuringDesk"
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

$ArtifactName = "TuringDesk-Native-Search-ARM64"
$Workflow = "native-search-windows.yml"
$DeployDir = Join-Path $env:LOCALAPPDATA "TuringDesk\NativeTest"
$DeployParent = Split-Path $DeployDir -Parent
$installTouched = $false

function Step([string]$Text) {
    Write-Host "`n==> $Text" -ForegroundColor Cyan
}

function Invoke-GhJson([string[]]$Arguments) {
    for ($attempt = 1; $attempt -le 5; $attempt++) {
        $output = @(& gh @Arguments 2>$null)
        if ($LASTEXITCODE -eq 0) {
            try {
                $text = [string]($output -join "`n")
                if ([string]::IsNullOrWhiteSpace($text)) { return $null }
                return ($text | ConvertFrom-Json)
            } catch { }
        }
        if ($attempt -lt 5) { Start-Sleep -Seconds ([Math]::Min($attempt * 2, 6)) }
    }
    throw "GitHub query failed after retries."
}

function Invoke-GhText([string[]]$Arguments) {
    for ($attempt = 1; $attempt -le 5; $attempt++) {
        $output = @(& gh @Arguments 2>$null)
        $text = [string]($output -join "`n")
        if ($LASTEXITCODE -eq 0 -and -not [string]::IsNullOrWhiteSpace($text)) { return $text.Trim() }
        if ($attempt -lt 5) { Start-Sleep -Seconds ([Math]::Min($attempt * 2, 6)) }
    }
    throw "GitHub query failed after retries."
}

function Test-BuildRelevantPath([string]$Path) {
    if ($Path -eq "CMakeLists.txt") { return $true }
    if ($Path -like "src/native/*") { return $true }
    if ($Path -like "runtime/arm64/*") { return $true }
    if ($Path -eq "scripts/prepare-third-party-runtime-arm64.ps1") { return $true }
    if ($Path -eq "scripts/verify-arm64-runtime-bundle.ps1") { return $true }
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
    if (-not $run) { throw "No validated TuringDesk ARM64 build is available." }

    $buildSha = [string]$run.headSha
    if ($buildSha -ne $MainSha) {
        $compare = Invoke-GhJson @("api", "repos/$Repo/compare/$buildSha...$MainSha")
        if (-not $compare) { throw "Unable to compare the validated build with current main." }
        if ([string]$compare.status -notin @("ahead", "identical")) {
            throw "Current main is not a clean forward descendant of the validated build."
        }
        $relevant = @($compare.files | Where-Object { Test-BuildRelevantPath ([string]$_.filename) })
        if ($relevant.Count -gt 0) {
            $names = ($relevant | ForEach-Object { [string]$_.filename }) -join ", "
            throw ("Current main contains unvalidated build/runtime changes: {0}" -f $names)
        }
        Write-Host "Latest validated package is still valid; newer changes are updater/docs only." -ForegroundColor DarkGray
    }

    return [pscustomobject]@{ RunId = [long]$run.databaseId; BuildSha = $buildSha }
}

function Download-Artifact([long]$RunId, [string]$Destination) {
    Step ("Downloading validated ARM64 binaries from run {0}" -f $RunId)
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    & gh run download $RunId --repo $Repo --name $ArtifactName --dir $Destination | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "Unable to download validated ARM64 artifact." }
}

function Materialize-Runtime([string]$Destination, [string]$BuildSha) {
    Step "Materializing matching TuringDesk RuntimeBundle"
    if (-not (Get-Command git -ErrorAction SilentlyContinue)) { throw "Git was not found in PATH." }

    $runtimeRepo = Join-Path $env:TEMP ("TuringDesk-RuntimeSource-" + [guid]::NewGuid().ToString("N"))
    try {
        & git clone --filter=blob:none --no-checkout "https://github.com/$Repo.git" $runtimeRepo | Out-Host
        if ($LASTEXITCODE -ne 0) { throw "Unable to fetch RuntimeBundle source." }
        & git -C $runtimeRepo sparse-checkout init --cone | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "Unable to initialize sparse checkout." }
        & git -C $runtimeRepo sparse-checkout set runtime/arm64 scripts | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "Unable to select RuntimeBundle files." }
        & git -C $runtimeRepo checkout $BuildSha | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "Unable to checkout RuntimeBundle revision $BuildSha." }
        $runtimeSha = (& git -C $runtimeRepo rev-parse HEAD).Trim()
        if ($LASTEXITCODE -ne 0 -or $runtimeSha -ne $BuildSha) {
            throw "RuntimeBundle revision mismatch."
        }

        $prepare = Join-Path $runtimeRepo "scripts\prepare-third-party-runtime-arm64.ps1"
        if (-not (Test-Path $prepare -PathType Leaf)) { throw "Runtime preparation script is missing." }
        & powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File $prepare -DeployDir $Destination -SkipGozServiceInstall | Out-Host
        if ($LASTEXITCODE -ne 0) { throw "RuntimeBundle preparation failed." }
    }
    finally {
        Remove-Item $runtimeRepo -Recurse -Force -ErrorAction SilentlyContinue
    }
}

function Assert-File([string]$Path, [string]$Label) {
    if (-not (Test-Path $Path -PathType Leaf)) { throw ("Package is missing {0}: {1}" -f $Label, $Path) }
}

function Test-Binary([string]$Exe, [string]$Name, [string[]]$Arguments = @("--self-test")) {
    Write-Host ("Testing {0}..." -f $Name) -ForegroundColor DarkGray
    $process = Start-Process -FilePath $Exe -ArgumentList $Arguments -Wait -PassThru -NoNewWindow
    if ($process.ExitCode -ne 0) { throw ("{0} test failed with exit code {1}." -f $Name, $process.ExitCode) }
}

function Test-StagedPackage([string]$Root) {
    Step "Running full package self-tests before installation"
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
        throw "File index service executable is missing: $Exe"
    }
    try {
        $process = Start-Process -FilePath $Exe -ArgumentList $Arguments -Verb RunAs -Wait -PassThru
        if (-not $process -or $process.ExitCode -ne 0) {
            if (-not $IgnoreFailure) { throw ("File index service operation failed: {0}" -f $Arguments) }
        }
    } catch {
        if (-not $IgnoreFailure) { throw }
    }
}

function Probe([string]$Exe, [string[]]$Arguments) {
    $out = Join-Path $env:TEMP ("td-update-probe-o-" + [guid]::NewGuid().ToString("N"))
    $err = Join-Path $env:TEMP ("td-update-probe-e-" + [guid]::NewGuid().ToString("N"))
    try {
        $p = Start-Process -FilePath $Exe -ArgumentList $Arguments -Wait -PassThru -NoNewWindow -RedirectStandardOutput $out -RedirectStandardError $err -ErrorAction SilentlyContinue
        if (-not $p) { return -1 }
        return [int]$p.ExitCode
    } catch {
        return -1
    }
    finally {
        Remove-Item $out,$err -Force -ErrorAction SilentlyContinue
    }
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

$work = $null
$next = $null
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

    Download-Artifact $validated.RunId $artifact
    Materialize-Runtime $next $validated.BuildSha
    Copy-Item (Join-Path $artifact "*") $next -Recurse -Force
    Set-Content (Join-Path $next ".installed-build-sha") -Value $validated.BuildSha -Encoding ASCII

    Test-StagedPackage $next

    $installTouched = $true
    $oldIndexService = Join-Path $DeployDir "Goz\gozd.exe"
    Stop-DeployedProcesses
    Invoke-ElevatedIndexService $oldIndexService "uninstall" -IgnoreFailure
    Stop-DeployedProcesses

    Step "Installing validated TuringDesk ARM64 package"
    if (Test-Path $DeployDir) { Remove-Item -LiteralPath $DeployDir -Recurse -Force -ErrorAction Stop }
    if (Test-Path $DeployDir) { throw "Existing TuringDesk installation directory could not be removed: $DeployDir" }
    Move-Item -LiteralPath $next -Destination $DeployDir -ErrorAction Stop
    $next = $null

    $newIndexService = Join-Path $DeployDir "Goz\gozd.exe"
    $newIndexClient = Join-Path $DeployDir "Goz\goz.exe"
    Assert-File $newIndexService "installed file index service"
    Assert-File $newIndexClient "installed file index client"
    Invoke-ElevatedIndexService $newIndexService "install"
    Wait-IndexReady $newIndexClient

    Step "Running installed package self-tests"
    Test-Binary (Join-Path $DeployDir "TuringDesk.exe") "TuringDesk"
    Test-Binary (Join-Path $DeployDir "TuringDeskWallpaper.exe") "TuringDesk Wallpaper"
    Test-Binary (Join-Path $DeployDir "TuringDeskHarness.exe") "TuringDesk Advanced Workbench"
    Test-Binary (Join-Path $DeployDir "TuringDeskHarness.exe") "TuringDesk Advanced Workbench smoke" @("--harness-smoke-test")

    Step "Starting TuringDesk"
    $wallpaper = Join-Path $DeployDir "TuringDeskWallpaper.exe"
    if (Should-ShowWallpaperSettings) { Start-Process -FilePath $wallpaper -ArgumentList "--settings" }
    else { Start-Process -FilePath $wallpaper }
    Start-Process -FilePath (Join-Path $DeployDir "TuringDesk.exe")

    Write-Host "`nTuringDesk update completed successfully." -ForegroundColor Green
    Write-Host ("Installed validated build: {0}" -f $validated.BuildSha) -ForegroundColor Green
    Write-Host ("GitHub Actions run: {0}" -f $validated.RunId) -ForegroundColor DarkGray
    Write-Host ("Install path: {0}" -f $DeployDir) -ForegroundColor DarkGray
}
catch {
    Write-Host "`nTuringDesk update failed." -ForegroundColor Red
    Write-Host $_.Exception.Message -ForegroundColor Red
    if ($installTouched) {
        Write-Host "The install phase had started. Automatic rollback is not performed; run the updater again after fixing the reported error." -ForegroundColor Yellow
    } else {
        Write-Host "The existing installation was not modified." -ForegroundColor Yellow
    }
    exit 1
}
finally {
    if ($work) { Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue }
    if ($next -and (Test-Path $next)) { Remove-Item $next -Recurse -Force -ErrorAction SilentlyContinue }
}
