param(
    # Clears stale wallpaper runtime diagnostics and re-syncs Enabled=1.
    # Keeps wallpaper selection, widgets, library media, and AI settings.
    [switch]$ResetWallpaperRuntime,

    # Resets all persisted desktop state (wallpaper.ini, widgets, library ini).
    # Keeps DevPreview/NativeTest binaries, Runtime/Pi/Goz, and AI model settings.
    [switch]$ResetDesktopConfig
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$RepoRoot = Split-Path $PSScriptRoot -Parent
$UserDataRoot = Join-Path $env:LOCALAPPDATA 'TuringDesk'
$PreviewRoot = Join-Path $UserDataRoot 'DevPreview'
$InstalledRoot = Join-Path $UserDataRoot 'NativeTest'
$Workflow = 'native-arm64-preview.yml'
$Repository = 'GoodLoongStudio/TuringDesk'

function Step([string]$Text) { Write-Host "`n==> $Text" -ForegroundColor Cyan }
function Info([string]$Text) { Write-Host $Text -ForegroundColor DarkGray }
function Warn([string]$Text) { Write-Host $Text -ForegroundColor Yellow }
function Show-DesktopConfigGuide {
    Write-Host ''
    Write-Host 'TuringDesk user data (under %LOCALAPPDATA%\TuringDesk):' -ForegroundColor Cyan
    Info '  DevPreview\              preview binaries (this script replaces each run)'
    Info '  NativeTest\              installed ARM64 test build + Runtime/Pi/Goz'
    Info '  wallpaper.ini            wallpaper enable/scene/layout + [Diagnostics] mount state'
    Info '  DesktopWidgets\          widgets.ini + widget packages'
    Info '  WallpaperLibrary\        library manifest, monitor-assignments, automation rules'
    Info '  model-settings.json      AI model/API settings (kept by reset switches below)'
    Info '  l3-sessions\             AI conversation cache (kept by reset switches below)'
    Info '  HKCU\Software\TuringDesk\WindowPlacement   window positions'
    Write-Host ''
    Write-Host 'When wallpaper stop/resume misbehaves after an update, try:' -ForegroundColor Yellow
    Write-Host '  .\scripts\download-arm64-preview.ps1 -ResetWallpaperRuntime' -ForegroundColor Yellow
    Write-Host 'For a clean desktop state while keeping AI settings and binaries:' -ForegroundColor Yellow
    Write-Host '  .\scripts\download-arm64-preview.ps1 -ResetDesktopConfig' -ForegroundColor Yellow
}
function Stop-TuringDeskProcesses {
    foreach ($name in @('TuringDesk', 'TuringDeskWallpaper', 'TuringDeskHarness')) {
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
function Remove-IniSection([string]$Path, [string]$Section) {
    if (-not (Test-Path $Path -PathType Leaf)) { return }
    $encoding = if ((Get-Content $Path -Encoding Byte -TotalCount 2) -join '' -eq '255254') { 'Unicode' } else { 'UTF8' }
    $lines = Get-Content $Path -Encoding $encoding
    $output = New-Object System.Collections.Generic.List[string]
    $skip = $false
    foreach ($line in $lines) {
        if ($line -match '^\s*\[(.+)\]\s*$') {
            $skip = ($Matches[1] -eq $Section)
            if (-not $skip) { [void]$output.Add($line) }
            continue
        }
        if (-not $skip) { [void]$output.Add($line) }
    }
    Set-Content -Path $Path -Value $output -Encoding $encoding
}
function Set-IniValue([string]$Path, [string]$Section, [string]$Key, [string]$Value) {
    if (-not (Test-Path $Path -PathType Leaf)) { return }
    $encoding = if ((Get-Content $Path -Encoding Byte -TotalCount 2) -join '' -eq '255254') { 'Unicode' } else { 'UTF8' }
    $lines = Get-Content $Path -Encoding $encoding
    $output = New-Object System.Collections.Generic.List[string]
    $inSection = $false
    $updated = $false
    foreach ($line in $lines) {
        if ($line -match '^\s*\[(.+)\]\s*$') {
            if ($inSection -and -not $updated) {
                [void]$output.Add("$Key=$Value")
                $updated = $true
            }
            $inSection = ($Matches[1] -eq $Section)
            [void]$output.Add($line)
            continue
        }
        if ($inSection -and ($line -match "^\s*$([regex]::Escape($Key))\s*=")) {
            [void]$output.Add("$Key=$Value")
            $updated = $true
            continue
        }
        [void]$output.Add($line)
    }
    if (-not $updated) {
        if ($output.Count -gt 0 -and -not [string]::IsNullOrWhiteSpace($output[$output.Count - 1])) {
            [void]$output.Add('')
        }
        [void]$output.Add("[$Section]")
        [void]$output.Add("$Key=$Value")
    }
    Set-Content -Path $Path -Value $output -Encoding $encoding
}
function Reset-WallpaperRuntimeConfig {
    Step 'Resetting wallpaper runtime config'
    Stop-TuringDeskProcesses
    $backupRoot = Join-Path $UserDataRoot ("ConfigBackup-wallpaper-runtime-" + (Get-Date -Format 'yyyyMMdd-HHmmss'))
    New-Item -ItemType Directory -Force -Path $backupRoot | Out-Null
    $wallpaperIni = Join-Path $UserDataRoot 'wallpaper.ini'
    Backup-UserDataItem $wallpaperIni $backupRoot
    if (Test-Path $wallpaperIni -PathType Leaf) {
        Remove-IniSection $wallpaperIni 'Diagnostics'
        Set-IniValue $wallpaperIni 'Wallpaper' 'Enabled' '1'
        Warn "Cleared [Diagnostics] and set [Wallpaper] Enabled=1 in wallpaper.ini"
    } else {
        Warn 'wallpaper.ini not found; nothing to reset.'
    }
    Write-Host "Backup saved to: $backupRoot" -ForegroundColor Green
}
function Reset-DesktopConfigState {
    Step 'Resetting persisted desktop config'
    Stop-TuringDeskProcesses
    $backupRoot = Join-Path $UserDataRoot ("ConfigBackup-desktop-" + (Get-Date -Format 'yyyyMMdd-HHmmss'))
    New-Item -ItemType Directory -Force -Path $backupRoot | Out-Null
    foreach ($item in @(
        (Join-Path $UserDataRoot 'wallpaper.ini'),
        (Join-Path $UserDataRoot 'store-demo.ini'),
        (Join-Path $UserDataRoot 'DesktopWidgets'),
        (Join-Path $UserDataRoot 'WallpaperLibrary\monitor-assignments.ini'),
        (Join-Path $UserDataRoot 'WallpaperLibrary\automation.ini'),
        (Join-Path $UserDataRoot 'WallpaperLibrary\application-rules.ini'),
        (Join-Path $UserDataRoot 'WallpaperLibrary\library.ini')
    )) {
        Backup-UserDataItem $item $backupRoot
        if (Test-Path $item) { Remove-Item $item -Recurse -Force }
    }
    Write-Host "Desktop config reset complete. Backup saved to: $backupRoot" -ForegroundColor Green
    Warn 'Kept: DevPreview, NativeTest binaries, Runtime/Pi/Goz, model-settings.json, l3-sessions, WallpaperLibrary media.'
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
Set-Location $RepoRoot

if ($ResetDesktopConfig) {
    Reset-DesktopConfigState
} elseif ($ResetWallpaperRuntime) {
    Reset-WallpaperRuntimeConfig
}

Step 'Updating local main checkout'
& git pull --ff-only origin main
if ($LASTEXITCODE -ne 0) { throw "git pull failed: $LASTEXITCODE" }
$headSha = (& git rev-parse HEAD).Trim()
if ($headSha -notmatch '^[0-9a-f]{40}$') { throw 'Unable to resolve current HEAD.' }

Step "Finding ARM64 preview for $headSha"
$runs = @(Get-PreviewRuns $headSha 1)
$run = $null
$previewSha = $headSha
$reusedAncestor = $false

if ($runs.Count -gt 0) {
    $run = $runs[0]
}
else {
    $run = Find-ReusablePreview $headSha
    if ($null -ne $run) {
        $previewSha = [string]$run.headSha
        $reusedAncestor = $true
        Write-Host "No ARM64 binary-impacting changes since $previewSha; reusing that successful preview." -ForegroundColor Yellow
    }
    else {
        Write-Host 'No safe reusable preview exists for this main SHA; dispatching an ARM64 preview now.' -ForegroundColor Yellow
        & gh workflow run $Workflow --repo $Repository --ref main
        if ($LASTEXITCODE -ne 0) { throw 'Unable to dispatch ARM64 preview workflow.' }

        for ($i = 0; $i -lt 30 -and $null -eq $run; $i++) {
            Start-Sleep -Seconds 2
            $candidateRuns = @(Get-PreviewRuns $headSha 3)
            foreach ($candidate in $candidateRuns) {
                if ([string]$candidate.headSha -eq $headSha) {
                    $run = $candidate
                    break
                }
            }
        }
        if ($null -eq $run) { throw 'Preview workflow was dispatched but its exact-head run did not appear in time.' }
    }
}

if ($null -eq $run -or -not $run.databaseId) {
    throw 'Unable to resolve an ARM64 preview workflow run.'
}
if ([string]$run.headSha -notmatch '^[0-9a-f]{40}$') {
    throw "Preview run did not report a valid head SHA. runId=$($run.databaseId)"
}
if ([string]$run.headSha -ne $previewSha) {
    throw "Preview run SHA mismatch: run=$($run.headSha) expected=$previewSha checkout=$headSha"
}

if ($run.status -ne 'completed') {
    Step "Waiting for GitHub ARM64 preview run $($run.databaseId)"
    & gh run watch $run.databaseId --repo $Repository --exit-status
    if ($LASTEXITCODE -ne 0) { throw "ARM64 preview build failed. Run: gh run view $($run.databaseId) --repo $Repository --log-failed" }
}
elseif ($run.conclusion -ne 'success') {
    throw "ARM64 preview run $($run.databaseId) concluded '$($run.conclusion)'."
}

Step 'Downloading ARM64 preview artifact'
$temp = Join-Path $env:TEMP ("TuringDeskPreview-" + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $temp | Out-Null
try {
    $artifactName = "turingdesk-arm64-preview-$previewSha"
    & gh run download $run.databaseId --repo $Repository --name $artifactName --dir $temp
    if ($LASTEXITCODE -ne 0) { throw 'Preview artifact download failed.' }

    foreach ($requiredExe in @('TuringDesk.exe', 'TuringDeskWallpaper.exe', 'TuringDeskHarness.exe')) {
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

    Get-Process TuringDesk -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    Get-Process TuringDeskWallpaper -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    Get-Process TuringDeskHarness -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    if (Test-Path $PreviewRoot) { Remove-Item $PreviewRoot -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $PreviewRoot | Out-Null
    Copy-Item (Join-Path $temp '*') $PreviewRoot -Recurse -Force
    Set-Content -Path (Join-Path $PreviewRoot 'preview-build-sha.txt') -Value $previewSha -Encoding ASCII
    Set-Content -Path (Join-Path $PreviewRoot 'preview-checkout-sha.txt') -Value $headSha -Encoding ASCII

    foreach ($name in @('Runtime','Pi','Goz')) {
        Ensure-Junction (Join-Path $PreviewRoot $name) (Join-Path $InstalledRoot $name)
    }

    Step 'Starting ARM64 developer preview'
    Start-Process -FilePath (Join-Path $PreviewRoot 'TuringDesk.exe') -WorkingDirectory $PreviewRoot
    if ($reusedAncestor) {
        Write-Host "Preview binary SHA: $previewSha (safe ancestor reuse)" -ForegroundColor Green
        Write-Host "Checkout SHA:       $headSha" -ForegroundColor Green
    }
    else {
        Write-Host "Preview SHA: $previewSha" -ForegroundColor Green
    }
    Write-Host "Preview path: $PreviewRoot" -ForegroundColor Green
    Write-Host 'Desktop, Widget, AI API settings, and DeepSeek Harness UI: included' -ForegroundColor Green
    if (-not (Test-Path (Join-Path $PreviewRoot 'Pi') -PathType Container)) {
        Write-Host 'Installed Pi Runtime was not found; UI preview works, Pi calls may be unavailable.' -ForegroundColor Yellow
    }
    if (Test-StaleWallpaperRuntime) {
        Warn 'Detected stale wallpaper runtime diagnostics in wallpaper.ini.'
        Warn 'If stop/resume still fails, rerun with: .\scripts\download-arm64-preview.ps1 -ResetWallpaperRuntime'
    }
    Show-DesktopConfigGuide
}
finally {
    if (Test-Path $temp) { Remove-Item $temp -Recurse -Force -ErrorAction SilentlyContinue }
}
