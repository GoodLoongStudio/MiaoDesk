param(
    [ValidateSet("auto", "preview", "full")]
    [string]$Mode = "auto"
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

$RepoRoot = Split-Path $PSScriptRoot -Parent
$BuildDir = Join-Path $RepoRoot "build-dev-arm64"
$InstalledDir = Join-Path $env:LOCALAPPDATA "MiaoDesk\NativeTest"
$InstalledMarker = Join-Path $InstalledDir ".installed-build-sha"
$PreviewMarker = Join-Path $BuildDir ".last-preview-sha"

function Step([string]$Text) {
    Write-Host "`n==> $Text" -ForegroundColor Cyan
}

function Read-Sha([string]$Path) {
    if (-not (Test-Path $Path -PathType Leaf)) { return $null }
    $value = ([string](Get-Content $Path -Raw -ErrorAction SilentlyContinue)).Trim()
    if ($value -match '^[0-9a-fA-F]{40}$') { return $value }
    return $null
}

function Test-Commit([string]$Sha) {
    if ([string]::IsNullOrWhiteSpace($Sha)) { return $false }
    & git -C $RepoRoot cat-file -e "$Sha^{commit}" 2>$null
    return $LASTEXITCODE -eq 0
}

function Get-ChangedFiles([string]$BaseSha, [string]$HeadSha) {
    if (-not (Test-Commit $BaseSha)) { return @("__UNKNOWN_BASE__") }
    return @(& git -C $RepoRoot diff --name-only $BaseSha $HeadSha | ForEach-Object { $_.Trim().Replace('\','/') } | Where-Object { $_ })
}

function Any([string[]]$Files, [string[]]$Patterns) {
    if ($Files -contains "__UNKNOWN_BASE__") { return $true }
    foreach ($file in $Files) {
        foreach ($pattern in $Patterns) {
            if ($file -like $pattern) { return $true }
        }
    }
    return $false
}

function Ensure-Junction([string]$Link, [string]$Target) {
    if (-not (Test-Path $Target -PathType Container)) { return }
    if (Test-Path $Link) { return }
    New-Item -ItemType Junction -Path $Link -Target $Target | Out-Null
}

function Ensure-FastDeveloperConfigure {
    $cache = Join-Path $BuildDir "CMakeCache.txt"
    $needsConfigure = -not (Test-Path $cache -PathType Leaf)
    if (-not $needsConfigure) {
        $cacheText = [string](Get-Content $cache -Raw -ErrorAction SilentlyContinue)
        $needsConfigure = $cacheText -notmatch '(?m)^MIAODESK_DEV_FAST:BOOL=ON$'
    }

    if ($needsConfigure) {
        Step "Configuring fast local ARM64 developer build"
        & cmake -S $RepoRoot -B $BuildDir -A ARM64 -DMIAODESK_DEV_FAST=ON
        if ($LASTEXITCODE -ne 0) { throw "CMake configure failed: $LASTEXITCODE" }
    }
}

function Invoke-LocalPreview([string[]]$Targets, [string]$HeadSha) {
    if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
        throw "cmake was not found in PATH. Install Visual Studio C++/CMake tools first."
    }

    Ensure-FastDeveloperConfigure

    foreach ($target in $Targets) {
        Step ("Incremental build: {0}" -f $target)
        & cmake --build $BuildDir --config Release --target $target --parallel
        if ($LASTEXITCODE -ne 0) { throw ("Local ARM64 build failed for {0}: {1}" -f $target, $LASTEXITCODE) }
    }

    if ($Targets -contains "MiaoDesk") {
        $outputDir = Join-Path $BuildDir "src\native\Release"
        $exe = Join-Path $outputDir "MiaoDesk.exe"
        if (-not (Test-Path $exe -PathType Leaf)) { throw "Developer MiaoDesk.exe was not produced: $exe" }

        foreach ($name in @("Runtime", "Pi", "Goz")) {
            Ensure-Junction (Join-Path $outputDir $name) (Join-Path $InstalledDir $name)
        }

        Step "Restarting developer preview"
        Get-Process MiaoDesk -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
        Start-Process -FilePath $exe -WorkingDirectory $outputDir
        Write-Host ("Preview: {0}" -f $exe) -ForegroundColor Green
        if (-not (Test-Path (Join-Path $outputDir "Pi") -PathType Container)) {
            Write-Host "Pi Runtime is not installed beside the developer build; UI preview works, Pi calls may be unavailable." -ForegroundColor Yellow
        }
    }

    New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
    Set-Content -Path $PreviewMarker -Value $HeadSha -Encoding ASCII
}

function Ensure-ValidatedCurrentMain {
    $Updater = Join-Path $RepoRoot "scripts\update-miaodesk-arm64.ps1"
    if (-not (Test-Path $Updater -PathType Leaf)) {
        throw "Validated ARM64 updater is missing: $Updater"
    }

    Step "Installing the exact validated current main build"
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Updater
    if ($LASTEXITCODE -ne 0) {
        throw "Validated ARM64 updater failed: $LASTEXITCODE"
    }
}

if (-not (Get-Command git -ErrorAction SilentlyContinue)) { throw "git was not found in PATH." }
Set-Location $RepoRoot

$headSha = (& git rev-parse HEAD).Trim()
if ($Mode -eq "full") {
    Ensure-ValidatedCurrentMain
    exit 0
}

$baseSha = Read-Sha $PreviewMarker
if (-not (Test-Commit $baseSha)) { $baseSha = Read-Sha $InstalledMarker }
if (-not (Test-Commit $baseSha)) {
    $parent = @(& git rev-parse "$headSha^" 2>$null)
    if ($LASTEXITCODE -eq 0 -and $parent.Count -gt 0) { $baseSha = ([string]$parent[0]).Trim() }
}

$files = @(Get-ChangedFiles $baseSha $headSha)
Write-Host "Development scope:" -ForegroundColor DarkGray
$files | ForEach-Object { Write-Host ("  {0}" -f $_) -ForegroundColor DarkGray }

$globalNative = Any $files @("CMakeLists.txt", "src/native/CMakeLists.txt", "src/native/include/*")
$app = $globalNative -or (Any $files @(
    "src/native/src/app/*",
    "src/native/src/ui/search/*",
    "src/native/src/ui/ai/*",
    "src/native/src/ui/settings/*",
    "src/native/src/ai/*",
    "src/native/src/search/*",
    "src/native/src/desktop/control/*",
    "src/native/src/desktop/widgets/WidgetService.cpp",
    "src/native/src/desktop/widgets/DesktopWidgetStore.cpp",
    "src/native/src/desktop/wallpaper/WallpaperService.cpp",
    "src/native/src/desktop/wallpaper/library/*",
    "src/native/src/desktop/wallpaper/monitor/*",
    "src/native/src/harness/HarnessProcessManager.cpp",
    "src/native/src/harness/HarnessSettingsBridge.cpp"))
$wallpaper = $globalNative -or (Any $files @("src/native/src/desktop/wallpaper/*", "src/native/src/ui/wallpaper/*", "src/native/src/ui/widgets/*", "src/native/src/desktop/shell/*"))
$harness = $globalNative -or (Any $files @("src/native/src/harness/*"))
$widgetProbe = $globalNative -or (Any $files @("src/native/src/desktop/widgets/*Acceptance*"))

if ($Mode -eq "preview") {
    $targets = @("MiaoDesk")
}
else {
    $targets = @()
    if ($app) { $targets += "MiaoDesk" }
    if ($wallpaper) { $targets += "MiaoDeskWallpaper" }
    if ($harness) { $targets += "MiaoDeskHarness" }
    if ($widgetProbe) { $targets += "MiaoDeskWidgetAcceptance" }
    $targets = @($targets | Select-Object -Unique)
}

if ($targets.Count -eq 0) {
    Write-Host "`nNo native binary relevant to this development task changed. Nothing to compile." -ForegroundColor Green
    New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
    Set-Content -Path $PreviewMarker -Value $headSha -Encoding ASCII
    Write-Host "Use DEPLOY-NATIVE-ARM64.cmd full only for formal package validation/install." -ForegroundColor DarkGray
    exit 0
}

Write-Host ("Selected lightweight targets: {0}" -f ($targets -join ", ")) -ForegroundColor Green
Invoke-LocalPreview $targets $headSha
Write-Host "`nDeveloper preview completed. No GitHub Actions artifact or updater was used." -ForegroundColor Green