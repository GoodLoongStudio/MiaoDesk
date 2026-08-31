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
function Require([string]$Name) {
    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "$Name is required. Install GitHub CLI once, then run 'gh auth login'."
    }
}
function Stop-MiaoDeskProcesses {
    foreach ($name in @('MiaoDesk', 'MiaoDeskWallpaper', 'MiaoDeskHarness')) {
        Get-Process $name -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    }
    Start-Sleep -Milliseconds 250
}
function Remove-TreeRobust([string]$Path) {
    if (-not (Test-Path $Path)) { return }
    $empty = Join-Path $env:TEMP ("mdp-empty-" + [Guid]::NewGuid().ToString('N').Substring(0, 8))
    New-Item -ItemType Directory -Force -Path $empty | Out-Null
    try {
        & robocopy.exe $empty $Path /MIR /R:1 /W:1 /NFL /NDL /NJH /NJS /NP /XJ | Out-Null
    }
    finally {
        Remove-Item $empty -Force -ErrorAction SilentlyContinue
    }
    Remove-Item $Path -Recurse -Force -ErrorAction SilentlyContinue
    if (Test-Path $Path) { & cmd.exe /d /c "rd /s /q `"$Path`"" | Out-Null }
}
function Ensure-Junction([string]$Name) {
    $target = Join-Path $InstalledRoot $Name
    $link = Join-Path $PreviewRoot $Name
    if (-not (Test-Path $target -PathType Container)) { return $false }
    if (Test-Path $link) { return $true }
    New-Item -ItemType Junction -Path $link -Target $target | Out-Null
    return $true
}
function Convert-GhRuns([object]$RawJson) {
    $text = (@($RawJson) -join "`n").Trim()
    if ([string]::IsNullOrWhiteSpace($text)) { return @() }
    $parsed = $text | ConvertFrom-Json
    if ($null -eq $parsed) { return @() }
    return @($parsed | Where-Object { $null -ne $_ -and $_.databaseId })
}

Require git
Require gh
Require robocopy.exe
Set-Location $RepoRoot

$headSha = (& git rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $headSha -notmatch '^[0-9a-f]{40}$') {
    throw 'Unable to resolve current HEAD.'
}

Step "Finding fast ARM64 UI preview for $headSha"
$raw = & gh run list --repo $Repository --workflow $Workflow --commit $headSha --limit 5 --json databaseId,status,conclusion,headSha
if ($LASTEXITCODE -ne 0) { throw "Unable to query GitHub preview workflow. Run 'gh auth login' once." }
$runs = @(Convert-GhRuns $raw | Where-Object { [string]$_.headSha -eq $headSha })
$run = $runs | Where-Object { $_.status -eq 'completed' -and $_.conclusion -eq 'success' } | Select-Object -First 1
if ($null -eq $run) {
    $run = $runs | Where-Object { $_.status -ne 'completed' } | Select-Object -First 1
}
if ($null -eq $run) {
    $failed = $runs | Where-Object { $_.status -eq 'completed' -and $_.conclusion -ne 'success' } | Select-Object -First 1
    if ($null -ne $failed) {
        throw "ARM64 preview failed with '$($failed.conclusion)' (run $($failed.databaseId))."
    }
    throw 'No ARM64 preview run exists for current main yet.'
}

$artifactName = "miaodesk-arm64-ui-preview-$headSha"
$temp = Join-Path $env:TEMP ("mdp-fast-" + [Guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Force -Path $temp | Out-Null
try {
    Step "Downloading FAST UI artifact from run $($run.databaseId)"
    & gh run download $run.databaseId --repo $Repository --name $artifactName --dir $temp
    if ($LASTEXITCODE -ne 0) {
        if ($run.status -ne 'completed') {
            throw "Fast UI artifact is not uploaded yet. Run $($run.databaseId) is still building; retry after 'Upload fast UI preview' finishes. No full Runtime/Pi package will be downloaded."
        }
        throw "Fast UI artifact download failed: $artifactName"
    }

    foreach ($requiredExe in @('MiaoDesk.exe', 'MiaoDeskWallpaper.exe', 'MiaoDeskHarness.exe')) {
        if (-not (Test-Path (Join-Path $temp $requiredExe) -PathType Leaf)) {
            throw "Fast preview is missing $requiredExe."
        }
    }

    $marker = Join-Path $temp 'preview-build-sha.txt'
    if (-not (Test-Path $marker -PathType Leaf)) { throw 'Fast preview SHA marker is missing.' }
    $artifactSha = ([string](Get-Content $marker -Raw)).Trim()
    if ($artifactSha -ne $headSha) { throw "Fast preview SHA mismatch: artifact=$artifactSha checkout=$headSha" }

    Stop-MiaoDeskProcesses
    if (Test-Path $PreviewRoot) { Remove-TreeRobust $PreviewRoot }
    New-Item -ItemType Directory -Force -Path $PreviewRoot | Out-Null
    & robocopy.exe $temp $PreviewRoot /E /COPY:DAT /DCOPY:DAT /R:2 /W:1 /NFL /NDL /NJH /NJS /NP /XJ | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "Fast preview copy failed with robocopy exit code $LASTEXITCODE." }

    Set-Content -Path (Join-Path $PreviewRoot 'preview-checkout-sha.txt') -Value $headSha -Encoding ASCII
    Set-Content -Path (Join-Path $PreviewRoot 'preview-mode.txt') -Value 'FAST-UI' -Encoding ASCII

    $reused = New-Object System.Collections.Generic.List[string]
    $missing = New-Object System.Collections.Generic.List[string]
    foreach ($name in @('Runtime', 'Pi', 'Goz', 'Wallpapers', 'Assets')) {
        if (Ensure-Junction $name) { [void]$reused.Add($name) }
        else { [void]$missing.Add($name) }
    }

    if ($reused.Count -gt 0) {
        Write-Host ("Reusing installed components: " + ($reused -join ', ')) -ForegroundColor DarkGray
    }
    if ($missing.Count -gt 0) {
        Warn ("Fast UI mode intentionally did not download: " + ($missing -join ', '))
        Warn 'UI/input/search/chat-window acceptance still works; full Agent/runtime acceptance requires the full preview or NativeTest install.'
    }

    Step 'Starting FAST ARM64 developer preview'
    Start-Process -FilePath (Join-Path $PreviewRoot 'MiaoDesk.exe') -WorkingDirectory $PreviewRoot
    Write-Host "FAST UI preview SHA: $headSha" -ForegroundColor Green
    Write-Host "Workflow run:        $($run.databaseId)" -ForegroundColor Green
    Write-Host 'Downloaded: executables + DLLs only; no bundled Node/Harness/Pi/wallpaper payload.' -ForegroundColor Green
}
finally {
    Remove-TreeRobust $temp
}
