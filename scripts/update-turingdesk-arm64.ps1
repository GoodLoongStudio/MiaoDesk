param(
    [string]$Repo = "GoodLoongStudio/TuringDesk"
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

$ArtifactName = "TuringDesk-Native-Search-ARM64"
$Workflow = "native-search-windows.yml"
$DeployDir = Join-Path $env:LOCALAPPDATA "TuringDesk\NativeTest"
$DeployParent = Split-Path $DeployDir -Parent
$RuntimeStoreDir = Join-Path $DeployParent "RuntimeBundle"
$JournalPath = Join-Path $DeployParent "NativeTest.update-state.json"
$MutexName = "Local\TuringDeskArm64Updater"
$InstalledBuildMarker = Join-Path $DeployDir ".installed-build-sha"

function Step([string]$Text) { Write-Host "`n==> $Text" -ForegroundColor Cyan }

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

function Test-GhAuthentication {
    $stdout = Join-Path $env:TEMP ("td-gh-auth-out-" + [guid]::NewGuid().ToString("N") + ".txt")
    $stderr = Join-Path $env:TEMP ("td-gh-auth-err-" + [guid]::NewGuid().ToString("N") + ".txt")
    try {
        $gh = (Get-Command gh -ErrorAction Stop).Source
        $process = Start-Process -FilePath $gh -ArgumentList @("auth", "status", "-h", "github.com") -Wait -PassThru -NoNewWindow -RedirectStandardOutput $stdout -RedirectStandardError $stderr
        if (-not $process -or $process.ExitCode -ne 0) {
            $detail = ""
            if (Test-Path $stderr -PathType Leaf) { $detail = ([string](Get-Content $stderr -Raw -ErrorAction SilentlyContinue)).Trim() }
            if ([string]::IsNullOrWhiteSpace($detail)) { $detail = "GitHub CLI authentication is missing or invalid." }
            throw ("GitHub CLI is not authenticated for github.com. Run: gh auth login -h github.com --web`n{0}" -f $detail)
        }
        return $true
    } finally { Remove-Item $stdout,$stderr -Force -ErrorAction SilentlyContinue }
}

function Get-RunsForCommit([string]$Sha) {
    $result = Invoke-GhJson @("run", "list", "--repo", $Repo, "--workflow", $Workflow, "--commit", $Sha, "--limit", "50", "--json", "databaseId,headSha,status,conclusion,event,createdAt")
    if (-not $result) { return @() }
    return @($result)
}

function Wait-ForRun([long]$RunId) {
    Step ("Waiting for ARM64 validation run {0}" -f $RunId)
    & gh run watch $RunId --repo $Repo --exit-status | Out-Host
    if ($LASTEXITCODE -ne 0) {
        Write-Host "`nARM64 validation failed. Failed job log follows." -ForegroundColor Red
        & gh run view $RunId --repo $Repo --log-failed | Out-Host
        throw ("ARM64 validation failed in run {0}." -f $RunId)
    }
}

function Resolve-ValidatedRun([string]$MainSha) {
    Step "Resolving validated ARM64 package for current main"
    $runs = @(Get-RunsForCommit $MainSha)
    $success = $runs | Where-Object { $_.status -eq "completed" -and $_.conclusion -eq "success" } | Sort-Object createdAt -Descending | Select-Object -First 1
    if ($success) { return [pscustomobject]@{ RunId = [long]$success.databaseId; BuildSha = $MainSha } }

    $running = $runs | Where-Object { $_.status -ne "completed" } | Sort-Object createdAt -Descending | Select-Object -First 1
    if (-not $running) {
        $before = @($runs | ForEach-Object { [long]$_.databaseId })
        Step "Starting ARM64 validation for current main"
        & gh workflow run $Workflow --repo $Repo --ref main | Out-Host
        if ($LASTEXITCODE -ne 0) { throw "Unable to start ARM64 validation workflow." }
        for ($i = 0; $i -lt 90; $i++) {
            Start-Sleep -Seconds 2
            $candidateRuns = @(Get-RunsForCommit $MainSha)
            $running = $candidateRuns | Where-Object { ([long]$_.databaseId -notin $before) -and $_.status -ne "completed" } | Sort-Object createdAt -Descending | Select-Object -First 1
            if ($running) { break }
            $completed = $candidateRuns | Where-Object { ([long]$_.databaseId -notin $before) -and $_.status -eq "completed" } | Sort-Object createdAt -Descending | Select-Object -First 1
            if ($completed) {
                if ($completed.conclusion -eq "success") { return [pscustomobject]@{ RunId = [long]$completed.databaseId; BuildSha = $MainSha } }
                & gh run view ([long]$completed.databaseId) --repo $Repo --log-failed | Out-Host
                throw ("ARM64 validation failed in run {0}." -f $completed.databaseId)
            }
        }
        if (-not $running) { throw "ARM64 validation was started but its run could not be resolved." }
    }
    Wait-ForRun ([long]$running.databaseId)
    $verified = @(Get-RunsForCommit $MainSha) | Where-Object { $_.status -eq "completed" -and $_.conclusion -eq "success" } | Sort-Object createdAt -Descending | Select-Object -First 1
    if (-not $verified) { throw "ARM64 validation completed without a successful current-main run." }
    return [pscustomobject]@{ RunId = [long]$verified.databaseId; BuildSha = $MainSha }
}

function Download-Artifact([long]$RunId, [string]$Destination) {
    Step ("Downloading validated ARM64 package from run {0}" -f $RunId)
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    & gh run download $RunId --repo $Repo --name $ArtifactName --dir $Destination | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "Unable to download validated ARM64 artifact." }
}

function Get-RuntimeBundleKeyFromManifest([object]$Manifest) {
    if (-not $Manifest) { return $null }
    try {
        $parts = @([string]$Manifest.schema, ([string]$Manifest.architecture).ToLowerInvariant(), ([string]$Manifest.node.sha256).ToLowerInvariant(), ([string]$Manifest.deepseekHarness.sha256).ToLowerInvariant(), ([string]$Manifest.goz.sha256).ToLowerInvariant(), ([string]$Manifest.pi.sha256).ToLowerInvariant())
        if ($parts | Where-Object { [string]::IsNullOrWhiteSpace($_) }) { return $null }
        return ($parts -join "|")
    } catch { return $null }
}

function Get-LocalRuntimeBundleKey([string]$Root) {
    $manifestPath = Join-Path $Root "Runtime\runtime-manifest.json"
    if (-not (Test-Path $manifestPath -PathType Leaf)) { return $null }
    try { return Get-RuntimeBundleKeyFromManifest (Get-Content $manifestPath -Raw | ConvertFrom-Json) } catch { return $null }
}

function Get-RemoteRuntimeBundleKey([string]$BuildSha) {
    $encoded = Invoke-GhText @("api", "repos/$Repo/contents/runtime/arm64/runtime-manifest.json?ref=$BuildSha", "--jq", ".content")
    if ([string]::IsNullOrWhiteSpace($encoded)) { throw "Unable to read RuntimeBundle manifest for validated build." }
    try {
        $json = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String(($encoded -replace "\s", "")))
        return Get-RuntimeBundleKeyFromManifest ($json | ConvertFrom-Json)
    } catch { throw ("Unable to decode RuntimeBundle manifest for validated build: {0}" -f $_.Exception.Message) }
}

function Test-ReparsePoint([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return $false }
    try { return ((Get-Item -LiteralPath $Path -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 } catch { return $false }
}

function Remove-Junction([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return }
    if (-not (Test-ReparsePoint $Path)) { throw ("Refusing to remove non-junction RuntimeBundle path: {0}" -f $Path) }
    # Windows PowerShell 5.1 Remove-Item prompts for non-empty directory junctions and can
    # traverse reparse points. Directory.Delete maps to RemoveDirectory and removes only the
    # junction itself; the shared RuntimeBundle target is never enumerated or deleted.
    [IO.Directory]::Delete($Path, $false)
}

function New-Junction([string]$Link, [string]$Target) {
    if (-not (Test-Path -LiteralPath $Target -PathType Container)) { throw ("RuntimeBundle junction target is missing: {0}" -f $Target) }
    if (Test-Path -LiteralPath $Link) { Remove-Junction $Link }
    New-Item -ItemType Junction -Path $Link -Target $Target -Force | Out-Null
}

function Link-RuntimeBundle([string]$Destination, [string]$SourceRoot) {
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    foreach ($relative in @("Runtime", "Pi", "Goz")) {
        New-Junction (Join-Path $Destination $relative) (Join-Path $SourceRoot $relative)
    }
}

function Remove-DeploymentTree([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return }
    foreach ($relative in @("Runtime", "Pi", "Goz")) {
        $child = Join-Path $Path $relative
        if (Test-ReparsePoint $child) { Remove-Junction $child }
    }
    Remove-Item -LiteralPath $Path -Recurse -Force -Confirm:$false -ErrorAction Stop
}

function Try-LinkUnchangedRuntimeBundle([string]$Destination, [string]$BuildSha) {
    if (-not (Test-Path $DeployDir -PathType Container)) { return $false }
    foreach ($relative in @("Runtime", "Pi", "Goz")) { if (-not (Test-Path (Join-Path $DeployDir $relative) -PathType Container)) { return $false } }
    $localKey = Get-LocalRuntimeBundleKey $DeployDir
    if ([string]::IsNullOrWhiteSpace($localKey)) { return $false }
    $remoteKey = Get-RemoteRuntimeBundleKey $BuildSha
    if ([string]::IsNullOrWhiteSpace($remoteKey) -or $localKey -ne $remoteKey) {
        Write-Host "RuntimeBundle changed; refreshing pinned runtime once." -ForegroundColor DarkGray
        return $false
    }
    Step "Reusing unchanged local ARM64 RuntimeBundle"
    $storeKey = Get-LocalRuntimeBundleKey $RuntimeStoreDir
    if ($storeKey -eq $remoteKey) {
        Link-RuntimeBundle $Destination $RuntimeStoreDir
        Write-Host "RuntimeBundle unchanged; staging uses shared RuntimeBundle junctions." -ForegroundColor Green
    } else {
        Link-RuntimeBundle $Destination $DeployDir
        Write-Host "RuntimeBundle unchanged; staging temporarily links the current runtime. No deep copy was performed." -ForegroundColor Green
    }
    return $true
}

function Promote-ReusedRuntimeBundle([string]$StagedRoot) {
    $expectedKey = Get-LocalRuntimeBundleKey $DeployDir
    if ([string]::IsNullOrWhiteSpace($expectedKey)) { throw "Current RuntimeBundle manifest disappeared before promotion." }
    $storeKey = Get-LocalRuntimeBundleKey $RuntimeStoreDir
    if ($storeKey -eq $expectedKey) {
        Link-RuntimeBundle $StagedRoot $RuntimeStoreDir
        return
    }

    Step "Promoting RuntimeBundle to stable shared storage"
    $candidate = Join-Path $DeployParent ("RuntimeBundle.next-" + [guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Force -Path $candidate | Out-Null
    foreach ($relative in @("Runtime", "Pi", "Goz")) {
        $source = Join-Path $DeployDir $relative
        if (Test-ReparsePoint $source) { throw ("Unexpected RuntimeBundle junction while promoting: {0}" -f $source) }
        Move-Item -LiteralPath $source -Destination (Join-Path $candidate $relative) -ErrorAction Stop
    }

    $stale = $null
    if (Test-Path $RuntimeStoreDir -PathType Container) {
        $stale = Join-Path $DeployParent ("RuntimeBundle.stale-" + [guid]::NewGuid().ToString("N"))
        Move-Item -LiteralPath $RuntimeStoreDir -Destination $stale -ErrorAction Stop
    }
    Move-Item -LiteralPath $candidate -Destination $RuntimeStoreDir -ErrorAction Stop
    Link-RuntimeBundle $DeployDir $RuntimeStoreDir
    Link-RuntimeBundle $StagedRoot $RuntimeStoreDir
    Write-Host "RuntimeBundle promoted by top-level directory moves; node_modules was not copied." -ForegroundColor Green
    if ($stale) { Write-Host ("Old RuntimeBundle retained for deferred cleanup: {0}" -f $stale) -ForegroundColor DarkGray }
}

function Materialize-Runtime([string]$Destination, [string]$BuildSha) {
    Step "Materializing changed TuringDesk RuntimeBundle"
    if (-not (Get-Command git -ErrorAction SilentlyContinue)) { throw "Git was not found in PATH." }
    $runtimeRepo = Join-Path $env:TEMP ("TuringDesk-RuntimeSource-" + [guid]::NewGuid().ToString("N"))
    try {
        & git clone --filter=blob:none --no-checkout "https://github.com/$Repo.git" $runtimeRepo | Out-Host
        if ($LASTEXITCODE -ne 0) { throw "Unable to fetch RuntimeBundle source." }
        & git -C $runtimeRepo sparse-checkout init --cone | Out-Null
        & git -C $runtimeRepo sparse-checkout set runtime/arm64 scripts | Out-Null
        & git -C $runtimeRepo checkout $BuildSha | Out-Null
        if ($LASTEXITCODE -ne 0) { throw ("Unable to checkout RuntimeBundle revision {0}." -f $BuildSha) }
        $runtimeSha = (& git -C $runtimeRepo rev-parse HEAD).Trim()
        if ($runtimeSha -ne $BuildSha) { throw "RuntimeBundle revision mismatch." }
        $prepare = Join-Path $runtimeRepo "scripts\prepare-third-party-runtime-arm64.ps1"
        & powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File $prepare -DeployDir $Destination -SkipGozServiceInstall | Out-Host
        if ($LASTEXITCODE -ne 0) { throw "RuntimeBundle preparation failed." }
    } finally { Remove-Item $runtimeRepo -Recurse -Force -ErrorAction SilentlyContinue }
}

function Assert-File([string]$Path, [string]$Label) { if (-not (Test-Path $Path -PathType Leaf)) { throw ("Package is missing {0}: {1}" -f $Label, $Path) } }
function Test-Binary([string]$Exe, [string]$Name, [string[]]$Arguments = @("--self-test")) {
    Write-Host ("Testing {0}..." -f $Name) -ForegroundColor DarkGray
    $process = Start-Process -FilePath $Exe -ArgumentList $Arguments -Wait -PassThru -NoNewWindow
    if ($process.ExitCode -ne 0) { throw ("{0} test failed with exit code {1}." -f $Name, $process.ExitCode) }
}

function Test-StagedPackage([string]$Root) {
    Step "Running full package self-tests before installation"
    $search = Join-Path $Root "TuringDesk.exe"; $wallpaper = Join-Path $Root "TuringDeskWallpaper.exe"; $workbench = Join-Path $Root "TuringDeskHarness.exe"
    $node = Join-Path $Root "Runtime\Node\node.exe"; $workbenchCli = Join-Path $Root "Runtime\Node\node_modules\@deepseek-ai\dsh\lib\bin.js"; $agentCli = Join-Path $Root "Pi\node_modules\@earendil-works\pi-coding-agent\dist\cli.js"
    $goz = Join-Path $Root "Goz\goz.exe"; $gozd = Join-Path $Root "Goz\gozd.exe"
    Assert-File $search "TuringDesk.exe"; Assert-File $wallpaper "TuringDeskWallpaper.exe"; Assert-File $workbench "TuringDeskHarness.exe"; Assert-File $node "bundled AI runtime"; Assert-File $workbenchCli "advanced workbench runtime"; Assert-File $agentCli "agent runtime"; Assert-File $goz "file index client"; Assert-File $gozd "file index service"
    Test-Binary $search "TuringDesk"; Test-Binary $wallpaper "TuringDesk Wallpaper"; Test-Binary $workbench "TuringDesk Advanced Workbench"; Test-Binary $node "TuringDesk Agent Runtime" @($agentCli, "--version"); Test-Binary $workbench "TuringDesk Advanced Workbench smoke" @("--harness-smoke-test")
}

function Test-InDeploy([string]$Candidate) {
    try { $rootPath = [IO.Path]::GetFullPath($DeployDir).TrimEnd("\"); $candidatePath = [IO.Path]::GetFullPath($Candidate); return $candidatePath.Equals($rootPath, [StringComparison]::OrdinalIgnoreCase) -or $candidatePath.StartsWith($rootPath + "\", [StringComparison]::OrdinalIgnoreCase) } catch { return $false }
}
function Stop-DeployedProcesses {
    Step "Stopping currently installed TuringDesk processes"
    $names = @("TuringDesk.exe", "TuringDeskWallpaper.exe", "TuringDeskHarness.exe", "node.exe", "goz.exe", "gozd.exe")
    try { foreach ($process in @(Get-CimInstance Win32_Process -ErrorAction Stop)) { if ($names -notcontains [string]$process.Name) { continue }; $exe = [string]$process.ExecutablePath; if (-not $exe -or -not (Test-InDeploy $exe)) { continue }; & taskkill.exe /PID $process.ProcessId /T /F 2>$null | Out-Null } } catch { }
    Start-Sleep -Milliseconds 500
}
function Invoke-ElevatedIndexService([string]$Exe, [string]$Arguments, [switch]$IgnoreFailure) {
    if (-not (Test-Path $Exe -PathType Leaf)) { if ($IgnoreFailure) { return }; throw ("File index service executable is missing: {0}" -f $Exe) }
    try { $process = Start-Process -FilePath $Exe -ArgumentList $Arguments -Verb RunAs -Wait -PassThru; if (-not $process -or $process.ExitCode -ne 0) { if (-not $IgnoreFailure) { throw ("File index service operation failed: {0}" -f $Arguments) } } } catch { if (-not $IgnoreFailure) { throw } }
}
function Probe([string]$Exe, [string[]]$Arguments) {
    $out = Join-Path $env:TEMP ("td-update-probe-o-" + [guid]::NewGuid().ToString("N")); $err = Join-Path $env:TEMP ("td-update-probe-e-" + [guid]::NewGuid().ToString("N"))
    try { $p = Start-Process -FilePath $Exe -ArgumentList $Arguments -Wait -PassThru -NoNewWindow -RedirectStandardOutput $out -RedirectStandardError $err -ErrorAction SilentlyContinue; if (-not $p) { return -1 }; return [int]$p.ExitCode } catch { return -1 } finally { Remove-Item $out,$err -Force -ErrorAction SilentlyContinue }
}
function Wait-IndexReady([string]$IndexExe) { Assert-File $IndexExe "installed file index client"; for ($i=0; $i -lt 120; $i++) { if ((Probe $IndexExe @("--status")) -eq 0) { return }; Start-Sleep -Milliseconds 500 }; throw "TuringDesk file index service did not become reachable after installation." }
function Should-ShowWallpaperSettings { $config = Join-Path $env:LOCALAPPDATA "TuringDesk\wallpaper.ini"; if (-not (Test-Path $config -PathType Leaf)) { return $true }; try { return -not [bool](Select-String -Path $config -Pattern "^Version=3$" -ErrorAction Stop) } catch { return $true } }
function Start-DeployedTuringDesk([string]$Root) { $wallpaper = Join-Path $Root "TuringDeskWallpaper.exe"; $search = Join-Path $Root "TuringDesk.exe"; Assert-File $wallpaper "TuringDeskWallpaper.exe"; Assert-File $search "TuringDesk.exe"; if (Should-ShowWallpaperSettings) { Start-Process -FilePath $wallpaper -ArgumentList "--settings" } else { Start-Process -FilePath $wallpaper }; Start-Process -FilePath $search }

function Write-UpdateJournal([string]$PreviousPath, [bool]$HadExistingInstall) { New-Item -ItemType Directory -Force -Path $DeployParent | Out-Null; [ordered]@{ schema=1; hadExistingInstall=$HadExistingInstall; previousPath=$PreviousPath; createdUtc=[DateTime]::UtcNow.ToString("o") } | ConvertTo-Json -Compress | Set-Content $JournalPath -Encoding ASCII }
function Remove-UpdateJournal { if (Test-Path $JournalPath -PathType Leaf) { Remove-Item $JournalPath -Force -ErrorAction Stop } }
function Recover-InterruptedUpdate {
    if (-not (Test-Path $JournalPath -PathType Leaf)) { return $false }
    Step "Recovering an interrupted TuringDesk update"; $state = Get-Content $JournalPath -Raw | ConvertFrom-Json; if ([int]$state.schema -ne 1) { throw "Unsupported update recovery journal schema." }
    $hadExisting = [bool]$state.hadExistingInstall; $previousPath = [string]$state.previousPath; Stop-DeployedProcesses; Invoke-ElevatedIndexService (Join-Path $DeployDir "Goz\gozd.exe") "uninstall" -IgnoreFailure
    if ($hadExisting -and -not [string]::IsNullOrWhiteSpace($previousPath) -and (Test-Path $previousPath -PathType Container)) { if (Test-Path $DeployDir) { Remove-DeploymentTree $DeployDir }; Move-Item -LiteralPath $previousPath -Destination $DeployDir -ErrorAction Stop } elseif (-not $hadExisting) { if (Test-Path $DeployDir) { Remove-DeploymentTree $DeployDir } } elseif (-not (Test-Path $DeployDir -PathType Container)) { throw "Interrupted update recovery could not find either the current or previous installation." }
    if ($hadExisting -and (Test-Path $DeployDir -PathType Container)) { $restoredService=Join-Path $DeployDir "Goz\gozd.exe"; $restoredClient=Join-Path $DeployDir "Goz\goz.exe"; if (Test-Path $restoredService -PathType Leaf) { Invoke-ElevatedIndexService $restoredService "install"; if (Test-Path $restoredClient -PathType Leaf) { Wait-IndexReady $restoredClient } }; Start-DeployedTuringDesk $DeployDir }
    Remove-UpdateJournal; Write-Host "Interrupted update recovery completed." -ForegroundColor Green; return $true
}

$work=$null; $next=$null; $previous=$null; $swapStarted=$false; $hadExistingInstall=$false; $updateMutex=$null; $mutexHeld=$false; $recoveryPerformed=$false; $reusedRuntime=$false
try {
    New-Item -ItemType Directory -Force -Path $DeployParent | Out-Null
    $updateMutex = New-Object -TypeName System.Threading.Mutex -ArgumentList $false, $MutexName
    try { $mutexHeld = $updateMutex.WaitOne(0) } catch [System.Threading.AbandonedMutexException] { $mutexHeld = $true }
    if (-not $mutexHeld) { throw "Another TuringDesk update is already running." }
    $recoveryPerformed = Recover-InterruptedUpdate
    if (-not (Get-Command gh -ErrorAction SilentlyContinue)) { throw "GitHub CLI (gh) was not found in PATH." }
    Step "Checking GitHub authentication"; [void](Test-GhAuthentication); Write-Host "GitHub authentication OK." -ForegroundColor Green
    Step "Resolving current TuringDesk main"; $mainSha = Invoke-GhText @("api", "repos/$Repo/commits/main", "--jq", ".sha"); Write-Host ("main: {0}" -f $mainSha) -ForegroundColor DarkGray
    $validated = Resolve-ValidatedRun $mainSha; Write-Host ("validated build: {0} / run {1}" -f $validated.BuildSha, $validated.RunId) -ForegroundColor Green
    if (Test-Path $InstalledBuildMarker -PathType Leaf) { $installedSha=([string](Get-Content $InstalledBuildMarker -Raw -ErrorAction SilentlyContinue)).Trim(); if ($installedSha -eq $validated.BuildSha) { Write-Host "`nTuringDesk is already on the latest validated ARM64 build." -ForegroundColor Green; Write-Host ("Installed validated build: {0}" -f $validated.BuildSha) -ForegroundColor Green; return } }

    $work=Join-Path $env:TEMP ("TuringDesk-Updater-"+[guid]::NewGuid().ToString("N")); $artifact=Join-Path $work "artifact"; $next=Join-Path $DeployParent ("NativeTest.next-"+[guid]::NewGuid().ToString("N")); New-Item -ItemType Directory -Force -Path $work,$next,$DeployParent | Out-Null
    Download-Artifact $validated.RunId $artifact
    $reusedRuntime = Try-LinkUnchangedRuntimeBundle $next $validated.BuildSha
    if (-not $reusedRuntime) { Materialize-Runtime $next $validated.BuildSha }
    Copy-Item (Join-Path $artifact "*") $next -Recurse -Force
    Set-Content (Join-Path $next ".installed-build-sha") -Value $validated.BuildSha -Encoding ASCII
    Test-StagedPackage $next

    $hadExistingInstall = Test-Path $DeployDir -PathType Container; if ($hadExistingInstall) { $previous=Join-Path $DeployParent ("NativeTest.previous-"+[guid]::NewGuid().ToString("N")) }
    Write-UpdateJournal $previous $hadExistingInstall; $swapStarted=$true
    Stop-DeployedProcesses; Invoke-ElevatedIndexService (Join-Path $DeployDir "Goz\gozd.exe") "uninstall" -IgnoreFailure; Stop-DeployedProcesses
    if ($reusedRuntime) { Promote-ReusedRuntimeBundle $next }

    Step "Installing validated TuringDesk ARM64 package"
    if ($hadExistingInstall) { Move-Item -LiteralPath $DeployDir -Destination $previous -ErrorAction Stop }
    if (Test-Path $DeployDir) { throw ("TuringDesk install directory still exists after backup move: {0}" -f $DeployDir) }
    Move-Item -LiteralPath $next -Destination $DeployDir -ErrorAction Stop; $next=$null
    $newIndexService=Join-Path $DeployDir "Goz\gozd.exe"; $newIndexClient=Join-Path $DeployDir "Goz\goz.exe"; Assert-File $newIndexService "installed file index service"; Assert-File $newIndexClient "installed file index client"; Invoke-ElevatedIndexService $newIndexService "install"; Wait-IndexReady $newIndexClient
    Step "Running installed package self-tests"; Test-Binary (Join-Path $DeployDir "TuringDesk.exe") "TuringDesk"; Test-Binary (Join-Path $DeployDir "TuringDeskWallpaper.exe") "TuringDesk Wallpaper"; Test-Binary (Join-Path $DeployDir "TuringDeskHarness.exe") "TuringDesk Advanced Workbench"; Test-Binary (Join-Path $DeployDir "TuringDeskHarness.exe") "TuringDesk Advanced Workbench smoke" @("--harness-smoke-test")
    Step "Starting TuringDesk"; Start-DeployedTuringDesk $DeployDir; Remove-UpdateJournal; $swapStarted=$false
    if ($previous -and (Test-Path $previous)) { try { Remove-DeploymentTree $previous; $previous=$null } catch { Write-Host ("Previous package cleanup was deferred: {0}" -f $_.Exception.Message) -ForegroundColor Yellow } }
    Write-Host "`nTuringDesk update completed successfully." -ForegroundColor Green; Write-Host ("Installed validated build: {0}" -f $validated.BuildSha) -ForegroundColor Green; Write-Host ("GitHub Actions run: {0}" -f $validated.RunId) -ForegroundColor DarkGray; Write-Host ("Install path: {0}" -f $DeployDir) -ForegroundColor DarkGray
} catch {
    $failure=$_.Exception.Message; Write-Host "`nTuringDesk update failed." -ForegroundColor Red; Write-Host $failure -ForegroundColor Red
    if ($swapStarted) {
        try {
            Step "Rolling back TuringDesk installation"; Stop-DeployedProcesses; Invoke-ElevatedIndexService (Join-Path $DeployDir "Goz\gozd.exe") "uninstall" -IgnoreFailure
            if ($previous -and (Test-Path $previous -PathType Container)) { if (Test-Path $DeployDir) { Remove-DeploymentTree $DeployDir }; Move-Item -LiteralPath $previous -Destination $DeployDir -ErrorAction Stop; $previous=$null } elseif (-not $hadExistingInstall) { if (Test-Path $DeployDir) { Remove-DeploymentTree $DeployDir } }
            if ($hadExistingInstall -and (Test-Path $DeployDir -PathType Container)) { $restoredService=Join-Path $DeployDir "Goz\gozd.exe"; $restoredClient=Join-Path $DeployDir "Goz\goz.exe"; if (Test-Path $restoredService -PathType Leaf) { Invoke-ElevatedIndexService $restoredService "install"; if (Test-Path $restoredClient -PathType Leaf) { Wait-IndexReady $restoredClient } }; Start-DeployedTuringDesk $DeployDir; Write-Host "Rollback completed. The previous TuringDesk installation was restored." -ForegroundColor Green } else { Write-Host "Rollback completed. The failed package was removed." -ForegroundColor Yellow }
            Remove-UpdateJournal
        } catch { Write-Host ("Automatic rollback failed: {0}" -f $_.Exception.Message) -ForegroundColor Red; Write-Host ("Recovery journal is preserved at: {0}" -f $JournalPath) -ForegroundColor Yellow; if ($previous -and (Test-Path $previous -PathType Container)) { Write-Host ("Previous installation is preserved at: {0}" -f $previous) -ForegroundColor Yellow } }
    } else { if ($recoveryPerformed) { Write-Host "An earlier interrupted update was recovered before this failure." -ForegroundColor Yellow } else { Write-Host "The existing installation was not modified." -ForegroundColor Yellow } }
    exit 1
} finally {
    if ($work) { Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue }
    if ($next -and (Test-Path $next)) { try { Remove-DeploymentTree $next } catch { Write-Host ("Staging cleanup deferred: {0}" -f $_.Exception.Message) -ForegroundColor DarkGray } }
    if ($mutexHeld -and $updateMutex) { try { $updateMutex.ReleaseMutex() } catch { } }
    if ($updateMutex) { $updateMutex.Dispose() }
}
