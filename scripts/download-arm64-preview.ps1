$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$RepoRoot = Split-Path $PSScriptRoot -Parent
$PreviewRoot = Join-Path $env:LOCALAPPDATA 'TuringDesk\DevPreview'
$InstalledRoot = Join-Path $env:LOCALAPPDATA 'TuringDesk\NativeTest'
$Workflow = 'native-arm64-preview.yml'
$Repository = 'GoodLoongStudio/TuringDesk'

function Step([string]$Text) { Write-Host "`n==> $Text" -ForegroundColor Cyan }
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

Require git
Require gh
Set-Location $RepoRoot

Step 'Updating local main checkout'
& git pull --ff-only origin main
if ($LASTEXITCODE -ne 0) { throw "git pull failed: $LASTEXITCODE" }
$headSha = (& git rev-parse HEAD).Trim()
if ($headSha -notmatch '^[0-9a-f]{40}$') { throw 'Unable to resolve current HEAD.' }

Step "Finding ARM64 preview for $headSha"
$runJson = & gh run list --repo $Repository --workflow $Workflow --commit $headSha --limit 1 --json databaseId,status,conclusion,headSha
if ($LASTEXITCODE -ne 0) { throw "Unable to query GitHub preview workflow. Run 'gh auth login' once." }
$runs = @($runJson | ConvertFrom-Json)
if ($runs.Count -eq 0) {
    Write-Host 'No preview run exists for this exact main SHA yet; dispatching one now.' -ForegroundColor Yellow
    & gh workflow run $Workflow --repo $Repository --ref main
    if ($LASTEXITCODE -ne 0) { throw 'Unable to dispatch ARM64 preview workflow.' }
    Start-Sleep -Seconds 3
    for ($i = 0; $i -lt 20 -and $runs.Count -eq 0; $i++) {
        $runJson = & gh run list --repo $Repository --workflow $Workflow --commit $headSha --limit 1 --json databaseId,status,conclusion,headSha
        $runs = @($runJson | ConvertFrom-Json)
        if ($runs.Count -eq 0) { Start-Sleep -Seconds 3 }
    }
}
if ($runs.Count -eq 0) { throw 'Preview workflow was dispatched but its run did not appear in time.' }
$run = $runs[0]
if ([string]$run.headSha -ne $headSha) {
    throw "Preview run SHA mismatch: run=$($run.headSha) checkout=$headSha"
}

if ($run.status -ne 'completed') {
    Step "Waiting for GitHub ARM64 preview run $($run.databaseId)"
    & gh run watch $run.databaseId --repo $Repository --exit-status
    if ($LASTEXITCODE -ne 0) { throw "ARM64 preview build failed. Run: gh run view $($run.databaseId) --repo $Repository --log-failed" }
}
elseif ($run.conclusion -ne 'success') {
    throw "ARM64 preview run $($run.databaseId) concluded '$($run.conclusion)'."
}

Step 'Downloading exact-head preview artifact'
$temp = Join-Path $env:TEMP ("TuringDeskPreview-" + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $temp | Out-Null
try {
    $artifactName = "turingdesk-arm64-preview-$headSha"
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
        if ($artifactSha -ne $headSha) { throw "Preview SHA mismatch: artifact=$artifactSha checkout=$headSha" }
    }
    else {
        # upload-artifact v4 ignores hidden files by default. Older preview artifacts
        # therefore may lack .preview-build-sha. The run was already queried by exact
        # commit and the artifact name is exact-SHA scoped, so this remains safe.
        Write-Host 'Preview marker is absent in this older artifact; exact run SHA and artifact name matched.' -ForegroundColor Yellow
    }

    Get-Process TuringDesk -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    Get-Process TuringDeskWallpaper -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    Get-Process TuringDeskHarness -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    if (Test-Path $PreviewRoot) { Remove-Item $PreviewRoot -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $PreviewRoot | Out-Null
    Copy-Item (Join-Path $temp '*') $PreviewRoot -Recurse -Force
    Set-Content -Path (Join-Path $PreviewRoot 'preview-build-sha.txt') -Value $headSha -Encoding ASCII

    foreach ($name in @('Runtime','Pi','Goz')) {
        Ensure-Junction (Join-Path $PreviewRoot $name) (Join-Path $InstalledRoot $name)
    }

    Step 'Starting ARM64 developer preview'
    Start-Process -FilePath (Join-Path $PreviewRoot 'TuringDesk.exe') -WorkingDirectory $PreviewRoot
    Write-Host "Preview SHA: $headSha" -ForegroundColor Green
    Write-Host "Preview path: $PreviewRoot" -ForegroundColor Green
    Write-Host 'Desktop, Widget, AI API settings, and DeepSeek Harness UI: included' -ForegroundColor Green
    if (-not (Test-Path (Join-Path $PreviewRoot 'Pi') -PathType Container)) {
        Write-Host 'Installed Pi Runtime was not found; UI preview works, Pi calls may be unavailable.' -ForegroundColor Yellow
    }
}
finally {
    if (Test-Path $temp) { Remove-Item $temp -Recurse -Force -ErrorAction SilentlyContinue }
}
