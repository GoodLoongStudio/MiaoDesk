param(
    [string]$Repo = "GoodLoongStudio/TuringDesk"
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

$ArtifactName = "TuringDesk-Native-Search-ARM64"
$Workflow = "native-search-windows.yml"
$DeployDir = Join-Path $env:LOCALAPPDATA "TuringDesk\NativeTest"
$DeployParent = Split-Path $DeployDir -Parent
$RuntimeMarkerName = ".runtime-lock-blob-sha"
$InstalledBuildName = ".installed-build-sha"

function Step([string]$Text) {
    Write-Host "`n==> $Text" -ForegroundColor Cyan
}

function Get-GhText([string[]]$Arguments) {
    for ($attempt = 1; $attempt -le 5; $attempt++) {
        $output = @(& gh @Arguments 2>$null)
        $code = $LASTEXITCODE
        $text = [string]($output -join "`n")
        if ($code -eq 0 -and -not [string]::IsNullOrWhiteSpace($text)) {
            return $text.Trim()
        }
        if ($attempt -lt 5) {
            Write-Host "GitHub query failed (attempt $attempt/5); retrying..." -ForegroundColor Yellow
            Start-Sleep -Seconds ([Math]::Min($attempt * 2, 6))
        }
    }
    throw "GitHub query failed after retries"
}

function Get-GhJson([string[]]$Arguments) {
    for ($attempt = 1; $attempt -le 5; $attempt++) {
        $output = @(& gh @Arguments 2>$null)
        $code = $LASTEXITCODE
        if ($code -eq 0) {
            try {
                $text = [string]($output -join "`n")
                if ([string]::IsNullOrWhiteSpace($text)) { return $null }
                return ($text | ConvertFrom-Json)
            }
            catch { }
        }
        if ($attempt -lt 5) {
            Write-Host "GitHub JSON query failed (attempt $attempt/5); retrying..." -ForegroundColor Yellow
            Start-Sleep -Seconds ([Math]::Min($attempt * 2, 6))
        }
    }
    throw "GitHub JSON query failed after retries"
}

function Test-BuildRelevantPath([string]$Path) {
    if ($Path -eq "CMakeLists.txt") { return $true }
    if ($Path -like "src/native/*") { return $true }
    if ($Path -like "runtime/arm64/*") { return $true }
    if ($Path -eq "scripts/prepare-third-party-runtime-arm64.ps1") { return $true }
    if ($Path -eq "scripts/verify-arm64-runtime-bundle.ps1") { return $true }
    if ($Path -eq "scripts/verify-l3-runtime-contract.ps1") { return $true }
    if ($Path -eq "scripts/verify-codex-jsonl-wire.ps1") { return $true }
    if ($Path -eq "scripts/verify-runtime-log-paths.ps1") { return $true }
    if ($Path -eq ".github/workflows/native-search-windows.yml") { return $true }
    return $false
}

function Resolve-ValidatedRun([string]$MainSha) {
    Step "Finding latest successful ARM64 package"
    $runs = @(Get-GhJson @("run", "list", "--repo", $Repo, "--workflow", $Workflow, "--branch", "main", "--limit", "30", "--json", "databaseId,headSha,status,conclusion,createdAt"))
    $run = $runs | Where-Object { $_.status -eq "completed" -and $_.conclusion -eq "success" } |
        Sort-Object createdAt -Descending | Select-Object -First 1
    if (-not $run) {
        throw "No successful ARM64 build is available. This updater will not trigger or wait for CI."
    }

    $buildSha = [string]$run.headSha
    if ($buildSha -ne $MainSha) {
        $compare = Get-GhJson @("api", "repos/$Repo/compare/$buildSha...$MainSha")
        if (-not $compare) { throw "Unable to compare the latest successful build with current main" }
        if ([string]$compare.status -notin @("ahead", "identical")) {
            throw "Current main is not a clean forward descendant of the latest successful ARM64 build"
        }
        $relevant = @($compare.files | Where-Object { Test-BuildRelevantPath ([string]$_.filename) })
        if ($relevant.Count -gt 0) {
            $names = ($relevant | ForEach-Object { [string]$_.filename }) -join ", "
            throw "Current main has unvalidated build/runtime changes after the latest green ARM64 package: $names"
        }
        Write-Host "Latest green package is still valid; newer main changes are updater/docs-only." -ForegroundColor DarkGray
    }

    return [pscustomobject]@{
        RunId = [long]$run.databaseId
        BuildSha = $buildSha
    }
}

function Download-Artifact([long]$RunId, [string]$Destination) {
    Step "Downloading verified ARM64 artifact from run $RunId"
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    & gh run download $RunId --repo $Repo --name $ArtifactName --dir $Destination | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "Unable to download ARM64 artifact" }
}

function Materialize-Runtime([string]$Destination) {
    Step "Materializing pinned ARM64 RuntimeBundle"
    if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
        throw "Git was not found in PATH"
    }

    $runtimeRepo = Join-Path $env:TEMP ("TuringDesk-RuntimeSource-" + [guid]::NewGuid().ToString("N"))
    try {
        & git clone --filter=blob:none --no-checkout --depth 1 "https://github.com/$Repo.git" $runtimeRepo | Out-Host
        if ($LASTEXITCODE -ne 0) { throw "Unable to fetch the pinned RuntimeBundle source" }
        & git -C $runtimeRepo sparse-checkout init --cone | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "Unable to initialize sparse checkout" }
        & git -C $runtimeRepo sparse-checkout set runtime/arm64 scripts | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "Unable to select RuntimeBundle files" }
        & git -C $runtimeRepo checkout main | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "Unable to checkout RuntimeBundle files" }

        $prepare = Join-Path $runtimeRepo "scripts\prepare-third-party-runtime-arm64.ps1"
        & powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File $prepare -DeployDir $Destination -SkipGozServiceInstall | Out-Host
        if ($LASTEXITCODE -ne 0) { throw "RuntimeBundle preparation failed" }
    }
    finally {
        Remove-Item $runtimeRepo -Recurse -Force -ErrorAction SilentlyContinue
    }
}

function Copy-CachedRuntime([string]$Destination, [string]$RuntimeLockSha) {
    $marker = Join-Path $DeployDir $RuntimeMarkerName
    $runtime = Join-Path $DeployDir "Runtime"
    $goz = Join-Path $DeployDir "Goz"
    if (-not (Test-Path $marker -PathType Leaf) -or -not (Test-Path $runtime -PathType Container) -or -not (Test-Path $goz -PathType Container)) {
        return $false
    }
    if ((Get-Content $marker -Raw).Trim() -ne $RuntimeLockSha) { return $false }

    Step "Reusing already installed pinned RuntimeBundle"
    Copy-Item $runtime (Join-Path $Destination "Runtime") -Recurse -Force
    Copy-Item $goz (Join-Path $Destination "Goz") -Recurse -Force
    return $true
}

function Assert-File([string]$Path, [string]$Label) {
    if (-not (Test-Path $Path -PathType Leaf)) { throw "Package is missing $Label: $Path" }
}

function Test-Binary([string]$Exe, [string]$Name, [string[]]$Arguments = @("--self-test")) {
    Write-Host "Testing $Name..." -ForegroundColor DarkGray
    $p = Start-Process -FilePath $Exe -ArgumentList $Arguments -Wait -PassThru -NoNewWindow
    if ($p.ExitCode -ne 0) { throw "$Name test failed with exit code $($p.ExitCode)" }
}

function Test-StagedPackage([string]$Root) {
    Step "Running staged package self-tests before installation"
    $search = Join-Path $Root "TuringDesk.exe"
    $wallpaper = Join-Path $Root "TuringDeskWallpaper.exe"
    $harness = Join-Path $Root "TuringDeskHarness.exe"
    $node = Join-Path $Root "Runtime\Node\node.exe"
    $dsh = Join-Path $Root "Runtime\Node\node_modules\@deepseek-ai\dsh\lib\bin.js"
    $goz = Join-Path $Root "Goz\goz.exe"
    $gozd = Join-Path $Root "Goz\gozd.exe"
    $codex = Join-Path $Root "Codex\codex.exe"
    $relay = Join-Path $Root "CodexRelay\codex-relay.exe"

    Assert-File $search "TuringDesk.exe"
    Assert-File $wallpaper "TuringDeskWallpaper.exe"
    Assert-File $harness "TuringDeskHarness.exe"
    Assert-File $node "bundled Node"
    Assert-File $dsh "DeepSeek Harness"
    Assert-File $goz "goz"
    Assert-File $gozd "gozd"
    Assert-File $codex "Codex CLI"
    Assert-File $relay "Codex Relay"

    Test-Binary $search "Native Search"
    Test-Binary $wallpaper "Native Wallpaper"
    Test-Binary $harness "Native Harness shell"
    Test-Binary $codex "Codex CLI version" @("--version")
    Test-Binary $codex "Codex app-server" @("app-server", "--help")
    Test-Binary $relay "Codex Relay" @("--help")
}

function Stop-DeployedProcesses {
    Step "Stopping currently installed TuringDesk processes"
    $names = @("TuringDesk.exe", "TuringDeskWallpaper.exe", "TuringDeskHarness.exe", "node.exe", "codex.exe", "codex-relay.exe")
    try {
        foreach ($p in @(Get-CimInstance Win32_Process -ErrorAction Stop)) {
            if ($names -notcontains [string]$p.Name) { continue }
            $exe = [string]$p.ExecutablePath
            if (-not $exe) { continue }
            try {
                $root = [IO.Path]::GetFullPath($DeployDir).TrimEnd("\") + "\"
                $full = [IO.Path]::GetFullPath($exe)
                if ($full.StartsWith($root, [StringComparison]::OrdinalIgnoreCase)) {
                    & taskkill.exe /PID $p.ProcessId /T /F 2>$null | Out-Null
                }
            }
            catch { }
        }
    }
    catch { }
    foreach ($name in @("TuringDesk", "TuringDeskWallpaper", "TuringDeskHarness")) {
        Get-Process -Name $name -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    }
    Start-Sleep -Milliseconds 400
}

function Invoke-ElevatedGoz([string]$Exe, [string]$Arguments, [switch]$IgnoreFailure) {
    if (-not (Test-Path $Exe -PathType Leaf)) { return }
    try {
        $p = Start-Process -FilePath $Exe -ArgumentList $Arguments -Verb RunAs -Wait -PassThru
        if (-not $p -or $p.ExitCode -ne 0) {
            if (-not $IgnoreFailure) { throw "gozd $Arguments failed" }
        }
    }
    catch {
        if (-not $IgnoreFailure) { throw }
    }
}

function Wait-GozReady([string]$GozExe) {
    for ($i = 0; $i -lt 120; $i++) {
        $p = Start-Process -FilePath $GozExe -ArgumentList "--status" -Wait -PassThru -NoNewWindow -ErrorAction SilentlyContinue
        if ($p -and $p.ExitCode -eq 0) { return }
        Start-Sleep -Milliseconds 500
    }
    throw "goz service did not become reachable after installation"
}

function Should-ShowWallpaperSettings {
    $config = Join-Path $env:LOCALAPPDATA "TuringDesk\wallpaper.ini"
    if (-not (Test-Path $config -PathType Leaf)) { return $true }
    try {
        return -not [bool](Select-String -Path $config -Pattern "^Version=3$" -ErrorAction Stop)
    }
    catch { return $true }
}

try {
    if (-not (Get-Command gh -ErrorAction SilentlyContinue)) {
        throw "GitHub CLI (gh) was not found in PATH"
    }
    Step "Checking GitHub authentication"
    & gh auth status | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "GitHub CLI is not authenticated. Run: gh auth login" }

    Step "Resolving current main"
    $mainSha = Get-GhText @("api", "repos/$Repo/commits/main", "--jq", ".sha")
    Write-Host "main: $mainSha" -ForegroundColor DarkGray

    $validated = Resolve-ValidatedRun $mainSha
    Write-Host "validated build: $($validated.BuildSha) / run $($validated.RunId)" -ForegroundColor Green

    $runtimeLockSha = Get-GhText @("api", "repos/$Repo/contents/runtime/arm64/runtime-lock.json?ref=$mainSha", "--jq", ".sha")

    $work = Join-Path $env:TEMP ("TuringDesk-Updater-" + [guid]::NewGuid().ToString("N"))
    $artifact = Join-Path $work "artifact"
    $next = Join-Path $DeployParent ("NativeTest.next-" + [guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Force -Path $work,$next,$DeployParent | Out-Null

    try {
        Download-Artifact $validated.RunId $artifact

        if (-not (Copy-CachedRuntime $next $runtimeLockSha)) {
            Materialize-Runtime $next
        }

        Copy-Item (Join-Path $artifact "*") $next -Recurse -Force
        Set-Content (Join-Path $next $RuntimeMarkerName) -Value $runtimeLockSha -Encoding ASCII
        Set-Content (Join-Path $next $InstalledBuildName) -Value $validated.BuildSha -Encoding ASCII

        # Important: all of these tests run before the current installation is touched.
        # A failure stops here. No rollback or automatic repository/source change is performed.
        Test-StagedPackage $next

        $oldGozd = Join-Path $DeployDir "Goz\gozd.exe"
        Stop-DeployedProcesses
        Invoke-ElevatedGoz $oldGozd "uninstall" -IgnoreFailure

        Step "Installing validated ARM64 package"
        Remove-Item $DeployDir -Recurse -Force -ErrorAction SilentlyContinue
        Move-Item $next $DeployDir

        $newGozd = Join-Path $DeployDir "Goz\gozd.exe"
        $newGoz = Join-Path $DeployDir "Goz\goz.exe"
        Invoke-ElevatedGoz $newGozd "install"
        Wait-GozReady $newGoz

        Step "Running installed package self-tests"
        Test-Binary (Join-Path $DeployDir "TuringDesk.exe") "Installed Native Search"
        Test-Binary (Join-Path $DeployDir "TuringDeskWallpaper.exe") "Installed Native Wallpaper"
        Test-Binary (Join-Path $DeployDir "TuringDeskHarness.exe") "Installed Native Harness shell"

        Step "Running installed DeepSeek Harness smoke test"
        Test-Binary (Join-Path $DeployDir "TuringDeskHarness.exe") "DeepSeek Harness smoke" @("--harness-smoke-test")

        Step "Starting TuringDesk"
        $wallpaper = Join-Path $DeployDir "TuringDeskWallpaper.exe"
        if (Should-ShowWallpaperSettings) {
            Start-Process -FilePath $wallpaper -ArgumentList "--settings"
        }
        else {
            Start-Process -FilePath $wallpaper
        }
        Start-Process -FilePath (Join-Path $DeployDir "TuringDesk.exe")

        Write-Host "`nUPDATE PASSED" -ForegroundColor Green
        Write-Host "Installed validated ARM64 build: $($validated.BuildSha)" -ForegroundColor Green
        Write-Host "GitHub Actions run: $($validated.RunId)" -ForegroundColor DarkGray
        Write-Host "Path: $DeployDir" -ForegroundColor DarkGray
    }
    finally {
        Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
        if (Test-Path $next) { Remove-Item $next -Recurse -Force -ErrorAction SilentlyContinue }
    }
}
catch {
    Write-Host "`nUPDATE FAILED" -ForegroundColor Red
    Write-Host $_.Exception.Message -ForegroundColor Red
    Write-Host "No automatic rollback was performed." -ForegroundColor Yellow
    exit 1
}
