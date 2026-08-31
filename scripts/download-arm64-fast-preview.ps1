$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$RepoRoot = Split-Path $PSScriptRoot -Parent
$UserDataRoot = Join-Path $env:LOCALAPPDATA 'MiaoDesk'
$PreviewRoot = Join-Path $UserDataRoot 'DevPreview'
$RuntimeCacheRoot = Join-Path $UserDataRoot 'RuntimeCache'
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
    foreach ($name in @('MiaoDesk', 'MiaoDeskWallpaper', 'MiaoDeskHarness', 'node')) {
        Get-Process $name -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    }
    Start-Sleep -Milliseconds 250
}
function Remove-TreeRobust([string]$Path) {
    if (-not (Test-Path $Path)) { return }
    $item = Get-Item $Path -Force -ErrorAction SilentlyContinue
    if ($item -and ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        & cmd.exe /d /c "rmdir `"$Path`"" | Out-Null
        if (Test-Path $Path) { Remove-Item $Path -Force -ErrorAction SilentlyContinue }
        return
    }
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
function Test-ComponentReady([string]$Root, [string]$Name) {
    if ([string]::IsNullOrWhiteSpace($Root)) { return $false }
    switch ($Name) {
        'Runtime' { return Test-Path (Join-Path $Root 'Runtime\Node\node.exe') -PathType Leaf }
        'Pi' { return Test-Path (Join-Path $Root 'Pi\node_modules\@earendil-works\pi-coding-agent\dist\cli.js') -PathType Leaf }
        'Goz' { return Test-Path (Join-Path $Root 'Goz\goz.exe') -PathType Leaf }
        default { return Test-Path (Join-Path $Root $Name) -PathType Container }
    }
}
function Resolve-ReusableComponent([string]$Name) {
    foreach ($root in @($RuntimeCacheRoot, $InstalledRoot)) {
        if (-not (Test-ComponentReady $root $Name)) { continue }
        $candidate = Join-Path $root $Name
        if (Test-Path $candidate -PathType Container) { return $candidate }
    }
    return $null
}
function Ensure-Junction([string]$Name) {
    $target = Resolve-ReusableComponent $Name
    $link = Join-Path $PreviewRoot $Name
    if ([string]::IsNullOrWhiteSpace($target)) { return $false }

    if (Test-Path $link) {
        $previewReady = Test-ComponentReady $PreviewRoot $Name
        if ($previewReady) { return $true }
        Remove-TreeRobust $link
    }

    New-Item -ItemType Junction -Path $link -Target $target | Out-Null
    if (-not (Test-ComponentReady $PreviewRoot $Name)) {
        Remove-TreeRobust $link
        throw "Fast preview mounted $Name from '$target', but the expected runtime files are still not visible through '$link'."
    }
    return $true
}
function Test-AgentRuntimeReady([string]$Root) {
    return (Test-Path (Join-Path $Root 'Runtime\Node\node.exe') -PathType Leaf) -and
           (Test-Path (Join-Path $Root 'Pi\node_modules\@earendil-works\pi-coding-agent\dist\cli.js') -PathType Leaf)
}
function Convert-GhRuns([object]$RawJson) {
    $text = (@($RawJson) -join "`n").Trim()
    if ([string]::IsNullOrWhiteSpace($text)) { return @() }
    $parsed = $text | ConvertFrom-Json
    if ($null -eq $parsed) { return @() }
    return @($parsed | Where-Object { $null -ne $_ -and $_.databaseId })
}
function Get-RecentRuns([int]$Limit = 30) {
    $raw = & gh run list --repo $Repository --workflow $Workflow --limit $Limit --json databaseId,status,conclusion,headSha
    if ($LASTEXITCODE -ne 0) { throw "Unable to query GitHub preview workflow. Run 'gh auth login' once." }
    return @(Convert-GhRuns $raw)
}
function Test-BinaryImpact([string]$Path) {
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
function Find-ReusableRun([string]$HeadSha) {
    foreach ($candidate in @(Get-RecentRuns 30)) {
        $candidateSha = [string]$candidate.headSha
        if ($candidateSha -notmatch '^[0-9a-f]{40}$') { continue }
        if ($candidate.status -eq 'completed' -and $candidate.conclusion -ne 'success') { continue }

        & git merge-base --is-ancestor $candidateSha $HeadSha 2>$null
        if ($LASTEXITCODE -ne 0) { continue }
        $changed = @(& git diff --name-only "$candidateSha..$HeadSha")
        if ($LASTEXITCODE -ne 0) { continue }
        if (@($changed | Where-Object { Test-BinaryImpact $_ }).Count -eq 0) {
            return $candidate
        }
    }
    return $null
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

$previewSha = $headSha
$reusedAncestor = $false
if ($null -eq $run) {
    $failed = $runs | Where-Object { $_.status -eq 'completed' -and $_.conclusion -ne 'success' } | Select-Object -First 1
    if ($null -ne $failed) {
        throw "ARM64 preview failed with '$($failed.conclusion)' (run $($failed.databaseId))."
    }

    $run = Find-ReusableRun $headSha
    if ($null -eq $run) {
        throw 'No exact or safely reusable ARM64 preview run exists for current main.'
    }
    $previewSha = [string]$run.headSha
    $reusedAncestor = $true
    Write-Host "No binary-impacting change since $previewSha; reusing its FAST UI artifact." -ForegroundColor Yellow
}

$artifactName = "miaodesk-arm64-ui-preview-$previewSha"
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
    if ($artifactSha -ne $previewSha) { throw "Fast preview SHA mismatch: artifact=$artifactSha expected=$previewSha checkout=$headSha" }

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
        Write-Host ("Reusing local runtime components: " + ($reused -join ', ')) -ForegroundColor DarkGray
    }
    if ($missing.Count -gt 0) {
        Warn ("Fast UI mode intentionally did not download: " + ($missing -join ', '))
        Warn 'Run INIT-MIAODESK-ARM64-RUNTIME.cmd once to seed the persistent RuntimeCache, then future fast previews reuse it.'
        Warn 'UI/input/search/chat-window acceptance still works without it; Agent/runtime acceptance does not.'
    }

    if (Test-AgentRuntimeReady $RuntimeCacheRoot) {
        if (-not (Test-AgentRuntimeReady $PreviewRoot)) {
            throw "RuntimeCache is ready, but DevPreview cannot see Runtime/Node or Pi after mounting. Refusing to launch a broken Agent preview."
        }
        $nodePath = Join-Path $PreviewRoot 'Runtime\Node\node.exe'
        $piPath = Join-Path $PreviewRoot 'Pi\node_modules\@earendil-works\pi-coding-agent\dist\cli.js'
        Write-Host "Agent runtime mounted: $nodePath" -ForegroundColor DarkGray
        Write-Host "Pi runtime mounted:    $piPath" -ForegroundColor DarkGray
    }

    Step 'Starting FAST ARM64 developer preview'
    Start-Process -FilePath (Join-Path $PreviewRoot 'MiaoDesk.exe') -WorkingDirectory $PreviewRoot
    if ($reusedAncestor) {
        Write-Host "FAST binary SHA:      $previewSha (safe ancestor reuse)" -ForegroundColor Green
        Write-Host "Checkout SHA:         $headSha" -ForegroundColor Green
    }
    else {
        Write-Host "FAST UI preview SHA:  $previewSha" -ForegroundColor Green
    }
    Write-Host "Workflow run:         $($run.databaseId)" -ForegroundColor Green
    Write-Host 'Downloaded: executables + DLLs only; bundled runtime is reused from RuntimeCache/NativeTest.' -ForegroundColor Green
}
finally {
    Remove-TreeRobust $temp
}
