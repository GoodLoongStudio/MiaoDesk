$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$RepoRoot = Split-Path $PSScriptRoot -Parent
$UserDataRoot = Join-Path $env:LOCALAPPDATA 'MiaoDesk'
$PreviewRoot = Join-Path $UserDataRoot 'DevPreview'
$InstalledRoot = Join-Path $UserDataRoot 'NativeTest'
$Workflow = 'native-arm64-preview.yml'
$Repository = 'GoodLoongStudio/MiaoDesk'

function Step([string]$Text) { Write-Host "`n==> $Text" -ForegroundColor Cyan }
function Warn([string]$Text) { Write-Host $Text -ForegroundColor Yellow }
function Stop-MiaoDeskProcesses {
    foreach ($name in @('MiaoDesk', 'MiaoDeskWallpaper', 'MiaoDeskHarness')) {
        Get-Process $name -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    }
    Start-Sleep -Milliseconds 400
}
function Backup-UserDataItem([string]$Source, [string]$BackupRoot) {
    if (-not (Test-Path $Source)) { return }
    $leaf = Split-Path $Source -Leaf
    $destination = Join-Path $BackupRoot $leaf
    if (Test-Path $Source -PathType Container) {
        Copy-Item $Source $destination -Recurse -Force
    } else {
        Copy-Item $Source $destination -Force
    }
}
function Copy-TreeRobust([string]$Source, [string]$Destination) {
    if (-not (Test-Path $Source -PathType Container)) {
        throw "Copy source does not exist: $Source"
    }
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null

    # PowerShell 5.1 Copy-Item still trips over MAX_PATH in deeply nested Node packages.
    # Robocopy handles these runtime trees correctly on supported Windows builds.
    & robocopy.exe $Source $Destination /E /COPY:DAT /DCOPY:DAT /R:2 /W:1 /NFL /NDL /NJH /NJS /NP /XJ | Out-Null
    $code = $LASTEXITCODE
    if ($code -ge 8) {
        throw "Preview tree copy failed with robocopy exit code $code."
    }
}
function Remove-TreeRobust([string]$Path) {
    if (-not (Test-Path $Path)) { return }

    # Empty the tree with robocopy first so deeply nested Node package paths do not make
    # Windows PowerShell 5.1 Remove-Item fail. /XJ avoids following preview junctions.
    $empty = Join-Path $env:TEMP ("mdp-empty-" + [Guid]::NewGuid().ToString('N').Substring(0, 8))
    New-Item -ItemType Directory -Force -Path $empty | Out-Null
    try {
        & robocopy.exe $empty $Path /MIR /R:1 /W:1 /NFL /NDL /NJH /NJS /NP /XJ | Out-Null
        $code = $LASTEXITCODE
        if ($code -ge 8) {
            Warn "Could not fully empty long-path tree before cleanup (robocopy=$code): $Path"
        }
    }
    finally {
        Remove-Item $empty -Force -ErrorAction SilentlyContinue
    }

    # After mirroring an empty tree, the root itself is shallow and safe to remove.
    Remove-Item $Path -Recurse -Force -ErrorAction SilentlyContinue
    if (Test-Path $Path) {
        & cmd.exe /d /c "rd /s /q `"$Path`"" | Out-Null
    }
}
function Get-IniEncoding([string]$Path) {
    if (-not (Test-Path $Path -PathType Leaf)) { return 'UTF8' }
    $bytes = Get-Content $Path -Encoding Byte -TotalCount 2
    if ($bytes.Count -ge 2 -and $bytes[0] -eq 255 -and $bytes[1] -eq 254) { return 'Unicode' }
    return 'UTF8'
}
function Remove-IniSection([string]$Path, [string]$Section) {
    if (-not (Test-Path $Path -PathType Leaf)) { return $false }
    $encoding = Get-IniEncoding $Path
    $lines = Get-Content $Path -Encoding $encoding
    $output = New-Object System.Collections.Generic.List[string]
    $skip = $false
    $removed = $false
    foreach ($line in $lines) {
        if ($line -match '^\s*\[(.+)\]\s*$') {
            if ($skip) { $removed = $true }
            $skip = ($Matches[1] -eq $Section)
            if (-not $skip) { [void]$output.Add($line) }
            continue
        }
        if (-not $skip) { [void]$output.Add($line) }
    }
    if ($skip) { $removed = $true }
    if (-not $removed) { return $false }
    Set-Content -Path $Path -Value $output -Encoding $encoding
    return $true
}
function Test-StaleWallpaperRuntime {
    $wallpaperIni = Join-Path $UserDataRoot 'wallpaper.ini'
    if (-not (Test-Path $wallpaperIni -PathType Leaf)) { return $false }
    $text = Get-Content $wallpaperIni -Raw -ErrorAction SilentlyContinue
    if ([string]::IsNullOrWhiteSpace($text)) { return $false }
    if ($text -notmatch '\[Diagnostics\]') { return $false }
    if ($text -match 'LastMountError=\S') { return $true }
    if ($text -match 'WidgetRuntime=\S' -and
        $text -notmatch 'WidgetRuntime=.*WebView2' -and
        $text -notmatch 'WidgetRuntime=.*Native Direct2D') {
        return $true
    }
    return $false
}
function Repair-PreviewDesktopConfig {
    Step 'Preparing local desktop config for preview'
    Stop-MiaoDeskProcesses
    $wallpaperIni = Join-Path $UserDataRoot 'wallpaper.ini'
    if (-not (Test-Path $wallpaperIni -PathType Leaf)) {
        Write-Host 'No wallpaper.ini yet; skipping config repair.' -ForegroundColor DarkGray
        return
    }

    $text = Get-Content $wallpaperIni -Raw -ErrorAction SilentlyContinue
    if ([string]::IsNullOrWhiteSpace($text) -or $text -notmatch '\[Diagnostics\]') {
        Write-Host 'Desktop config looks current; no repair needed.' -ForegroundColor DarkGray
        return
    }

    $stale = Test-StaleWallpaperRuntime
    $backupRoot = Join-Path $UserDataRoot ("ConfigBackup-auto-" + (Get-Date -Format 'yyyyMMdd-HHmmss'))
    New-Item -ItemType Directory -Force -Path $backupRoot | Out-Null
    Backup-UserDataItem $wallpaperIni $backupRoot
    if (-not (Remove-IniSection $wallpaperIni 'Diagnostics')) {
        Write-Host 'Desktop config looks current; no repair needed.' -ForegroundColor DarkGray
        return
    }

    if ($stale) {
        Warn 'Cleared stale wallpaper [Diagnostics] before preview launch.'
    } else {
        Warn 'Cleared wallpaper [Diagnostics] before preview launch.'
    }
    Write-Host "Backup saved to: $backupRoot" -ForegroundColor Green
}
function Require([string]$Name) {
    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "$Name is required. Install GitHub CLI once, then run 'gh auth login'. No local CMake or Visual Studio is required for preview."
    }
}
function Ensure-Junction([string]$Link, [string]$Target) {
    if (-not (Test-Path $Target -PathType Container)) { return }
    if (Test-Path $Link) { return }
    New-Item -ItemType Junction -Path $Link -Target $Target | Out-Null
}
function Convert-GhRuns([object]$RawJson) {
    $text = (@($RawJson) -join "`n").Trim()
    if ([string]::IsNullOrWhiteSpace($text)) { return @() }
    $parsed = $text | ConvertFrom-Json
    if ($null -eq $parsed) { return @() }
    return @($parsed | Where-Object { $null -ne $_ -and $_.databaseId })
}
function Get-PreviewRuns([string]$Commit, [int]$Limit = 1) {
    $raw = & gh run list --repo $Repository --workflow $Workflow --commit $Commit --limit $Limit --json databaseId,status,conclusion,headSha
    if ($LASTEXITCODE -ne 0) { throw "Unable to query GitHub preview workflow. Run 'gh auth login' once." }
    return @(Convert-GhRuns $raw)
}
function Get-RecentPreviewRuns([int]$Limit = 20) {
    $raw = & gh run list --repo $Repository --workflow $Workflow --limit $Limit --json databaseId,status,conclusion,headSha
    if ($LASTEXITCODE -ne 0) { throw "Unable to query GitHub preview workflow. Run 'gh auth login' once." }
    return @(Convert-GhRuns $raw)
}
function Test-PreviewBinaryImpact([string]$Path) {
    if ([string]::IsNullOrWhiteSpace($Path)) { return $false }
    $normalized = $Path.Replace('\', '/')
    return ($normalized -eq 'CMakeLists.txt') -or
           ($normalized -eq 'CMakePresets.json') -or
           ($normalized -eq 'vcpkg.json') -or
           ($normalized -eq 'vcpkg-configuration.json') -or
           ($normalized -like 'cmake/*') -or
           ($normalized -like 'src/native/*') -or
           ($normalized -eq '.github/workflows/native-arm64-preview.yml')
}
function Find-ReusablePreview([string]$HeadSha) {
    foreach ($candidate in @(Get-RecentPreviewRuns 30)) {
        if ($candidate.status -ne 'completed' -or $candidate.conclusion -ne 'success') { continue }
        $candidateSha = [string]$candidate.headSha
        if ($candidateSha -notmatch '^[0-9a-f]{40}$') { continue }

        & git merge-base --is-ancestor $candidateSha $HeadSha 2>$null
        if ($LASTEXITCODE -ne 0) { continue }

        $changedPaths = @(& git diff --name-only "$candidateSha..$HeadSha")
        if ($LASTEXITCODE -ne 0) { continue }
        $impacting = @($changedPaths | Where-Object { Test-PreviewBinaryImpact $_ })
        if ($impacting.Count -eq 0) {
            return $candidate
        }
    }
    return $null
}

Require git
Require gh
Require robocopy.exe
Set-Location $RepoRoot

Repair-PreviewDesktopConfig

Step 'Updating local main checkout'
& git pull --ff-only origin main
if ($LASTEXITCODE -ne 0) { throw "git pull failed: $LASTEXITCODE" }
$headSha = (& git rev-parse HEAD).Trim()
if ($headSha -notmatch '^[0-9a-f]{40}$') { throw 'Unable to resolve current HEAD.' }

Step "Finding completed ARM64 preview for $headSha"
$runs = @(Get-PreviewRuns $headSha 3)
$run = $null
$previewSha = $headSha
$reusedAncestor = $false

foreach ($candidate in $runs) {
    if ([string]$candidate.headSha -ne $headSha) { continue }
    if ($candidate.status -eq 'completed' -and $candidate.conclusion -eq 'success') {
        $run = $candidate
        break
    }
}

if ($null -eq $run) {
    $active = $runs | Where-Object {
        [string]$_.headSha -eq $headSha -and $_.status -ne 'completed'
    } | Select-Object -First 1
    if ($null -ne $active) {
        throw "ARM64 preview for current main is already building automatically (run $($active.databaseId)). This shortcut never triggers or waits for GitHub Actions. Please run it again after the build completes."
    }

    $failed = $runs | Where-Object {
        [string]$_.headSha -eq $headSha -and $_.status -eq 'completed' -and $_.conclusion -ne 'success'
    } | Select-Object -First 1
    if ($null -ne $failed) {
        throw "ARM64 preview for current main finished with '$($failed.conclusion)' (run $($failed.databaseId)). This shortcut never re-runs GitHub Actions."
    }

    $run = Find-ReusablePreview $headSha
    if ($null -ne $run) {
        $previewSha = [string]$run.headSha
        $reusedAncestor = $true
        Write-Host "No ARM64 binary-impacting changes since $previewSha; reusing that successful preview." -ForegroundColor Yellow
    }
    else {
        throw 'No completed ARM64 preview exists for this binary-changing main SHA. This shortcut never triggers GitHub Actions. Wait for the automatic main build to finish, then run the shortcut again.'
    }
}

if ($null -eq $run -or -not $run.databaseId) {
    throw 'Unable to resolve a completed ARM64 preview workflow run.'
}
if ($run.status -ne 'completed' -or $run.conclusion -ne 'success') {
    throw "Resolved preview run $($run.databaseId) is not a completed successful build."
}
if ([string]$run.headSha -notmatch '^[0-9a-f]{40}$') {
    throw "Preview run did not report a valid head SHA. runId=$($run.databaseId)"
}
if ([string]$run.headSha -ne $previewSha) {
    throw "Preview run SHA mismatch: run=$($run.headSha) expected=$previewSha checkout=$headSha"
}

Step 'Downloading completed ARM64 preview artifact'
# Keep the staging root short. The bundled Pi/Node dependency graph contains paths that can
# exceed legacy MAX_PATH when combined with a long %TEMP% GUID directory.
$temp = Join-Path $env:TEMP ("mdp-" + [Guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Force -Path $temp | Out-Null
try {
    $artifactName = "miaodesk-arm64-preview-$previewSha"
    & gh run download $run.databaseId --repo $Repository --name $artifactName --dir $temp
    if ($LASTEXITCODE -ne 0) { throw 'Preview artifact download failed.' }

    foreach ($requiredExe in @('MiaoDesk.exe', 'MiaoDeskWallpaper.exe', 'MiaoDeskHarness.exe')) {
        $requiredPath = Join-Path $temp $requiredExe
        if (-not (Test-Path $requiredPath -PathType Leaf)) {
            throw "Downloaded preview does not contain $requiredExe. Settings/Harness UI would be unavailable."
        }
    }

    $marker = $null
    foreach ($candidate in @('preview-build-sha.txt', '.preview-build-sha')) {
        $candidatePath = Join-Path $temp $candidate
        if (Test-Path $candidatePath -PathType Leaf) {
            $marker = $candidatePath
            break
        }
    }

    if ($marker) {
        $artifactSha = ([string](Get-Content $marker -Raw)).Trim()
        if ($artifactSha -ne $previewSha) { throw "Preview SHA mismatch: artifact=$artifactSha expected=$previewSha checkout=$headSha" }
    }
    else {
        Write-Host 'Preview marker is absent in this older artifact; workflow run SHA and artifact name still match.' -ForegroundColor Yellow
    }

    Stop-MiaoDeskProcesses
    if (Test-Path $PreviewRoot) { Remove-TreeRobust $PreviewRoot }
    New-Item -ItemType Directory -Force -Path $PreviewRoot | Out-Null
    Copy-TreeRobust $temp $PreviewRoot
    Set-Content -Path (Join-Path $PreviewRoot 'preview-build-sha.txt') -Value $previewSha -Encoding ASCII
    Set-Content -Path (Join-Path $PreviewRoot 'preview-checkout-sha.txt') -Value $headSha -Encoding ASCII

    foreach ($name in @('Runtime','Pi','Goz')) {
        Ensure-Junction (Join-Path $PreviewRoot $name) (Join-Path $InstalledRoot $name)
    }

    Step 'Starting ARM64 developer preview'
    Start-Process -FilePath (Join-Path $PreviewRoot 'MiaoDesk.exe') -WorkingDirectory $PreviewRoot
    if ($reusedAncestor) {
        Write-Host "Preview binary SHA: $previewSha (safe ancestor reuse)" -ForegroundColor Green
        Write-Host "Checkout SHA:       $headSha" -ForegroundColor Green
    }
    else {
        Write-Host "Preview SHA: $previewSha" -ForegroundColor Green
    }
    Write-Host "Preview path: $PreviewRoot" -ForegroundColor Green
    Write-Host 'Desktop, Widget, AI API settings, Pi runtime, and DeepSeek Harness UI: included' -ForegroundColor Green
    if (-not (Test-Path (Join-Path $PreviewRoot 'Pi') -PathType Container)) {
        Write-Host 'Preview Pi Runtime is missing; AI UI works but Pi calls may be unavailable.' -ForegroundColor Yellow
    }
}
finally {
    if (Test-Path $temp) { Remove-TreeRobust $temp }
}
