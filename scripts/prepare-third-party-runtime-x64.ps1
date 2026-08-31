param(
    [string]$DeployDir = (Join-Path $env:LOCALAPPDATA 'MiaoDesk\NativeTest'),
    [switch]$SkipGozServiceInstall
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$RepoRoot = Split-Path $PSScriptRoot -Parent
$BundleRoot = Join-Path $RepoRoot 'runtime\x64'
$ManifestPath = Join-Path $BundleRoot 'runtime-manifest.json'
$CompleteMarker = Join-Path $BundleRoot '.complete'

function Step([string]$Text) { Write-Host "`n==> $Text" -ForegroundColor Cyan }
function Sha256([string]$Path) { (Get-FileHash -Algorithm SHA256 -Path $Path).Hash.ToLowerInvariant() }
function Resolve-BundleFile([string]$RelativePath) {
    $path = Join-Path $BundleRoot ($RelativePath -replace '/', '\')
    if (-not (Test-Path $path -PathType Leaf)) { throw "MiaoDesk x64 RuntimeBundle file is missing: $path" }
    $path
}
function Assert-BundleHash([string]$Path, [string]$Expected) {
    $actual = Sha256 $Path
    if ([string]::IsNullOrWhiteSpace($Expected) -or $actual -ne $Expected.ToLowerInvariant()) {
        throw "MiaoDesk x64 RuntimeBundle integrity check failed: $Path`nExpected: $Expected`nActual:   $actual"
    }
}
function Assert-File([string]$Path, [string]$Label) {
    if (-not (Test-Path $Path -PathType Leaf)) { throw ("MiaoDesk x64 RuntimeBundle is missing {0}: {1}" -f $Label, $Path) }
}
function Test-InDeploy([string]$Candidate) {
    try {
        $root = [IO.Path]::GetFullPath($DeployDir).TrimEnd('\')
        $path = [IO.Path]::GetFullPath($Candidate).TrimEnd('\')
        return $path.Equals($root, [StringComparison]::OrdinalIgnoreCase) -or
               $path.StartsWith($root + '\', [StringComparison]::OrdinalIgnoreCase)
    } catch { return $false }
}
function Stop-OwnedProcesses {
    $names = @('MiaoDesk.exe','MiaoDeskWallpaper.exe','MiaoDeskHarness.exe','node.exe','goz.exe','gozd.exe')
    try {
        foreach ($p in @(Get-CimInstance Win32_Process -ErrorAction Stop)) {
            if ($names -notcontains [string]$p.Name) { continue }
            $exe = [string]$p.ExecutablePath
            if (-not $exe -or -not (Test-InDeploy $exe)) { continue }
            & taskkill.exe /PID $p.ProcessId /T /F 2>$null | Out-Null
        }
    } catch { Write-Host "MiaoDesk process scan warning: $($_.Exception.Message)" -ForegroundColor DarkYellow }
    Start-Sleep -Milliseconds 250
}
function Expand-BundleArchive([string]$Archive, [string]$Destination) {
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    & tar.exe -xf $Archive -C $Destination
    if ($LASTEXITCODE -ne 0) { throw "Failed to extract MiaoDesk x64 RuntimeBundle archive: $Archive" }
}
function Invoke-ElevatedIndexService([string]$Exe, [string]$Arguments) {
    $p = Start-Process -FilePath $Exe -ArgumentList $Arguments -Verb RunAs -Wait -PassThru
    if (-not $p -or $p.ExitCode -ne 0) { throw "MiaoDesk file index service operation failed: $Arguments" }
}
function Probe([string]$Exe, [string[]]$Arguments) {
    $out = Join-Path $env:TEMP ('md-probe-o-' + [guid]::NewGuid().ToString('N'))
    $err = Join-Path $env:TEMP ('md-probe-e-' + [guid]::NewGuid().ToString('N'))
    try {
        $p = Start-Process -FilePath $Exe -ArgumentList $Arguments -Wait -PassThru -NoNewWindow -RedirectStandardOutput $out -RedirectStandardError $err
        [pscustomobject]@{ ExitCode=[int]$p.ExitCode; Text=((Get-Content $out,$err -Raw -ErrorAction SilentlyContinue) -join "`n") }
    } catch { [pscustomobject]@{ ExitCode=-1; Text=$_.Exception.Message } }
    finally { Remove-Item $out,$err -Force -ErrorAction SilentlyContinue }
}
function Ensure-IndexService([string]$IndexExe,[string]$IndexDaemon) {
    if ($SkipGozServiceInstall) { return }
    $status = Probe $IndexDaemon @('status')
    if ($status.ExitCode -ne 0 -or $status.Text -notmatch '(?i)Running') { Invoke-ElevatedIndexService $IndexDaemon 'install' }
    for ($i=0; $i -lt 120; $i++) {
        if ((Probe $IndexExe @('--status')).ExitCode -eq 0) { Write-Host 'MiaoDesk file search is ready.' -ForegroundColor Green; return }
        Start-Sleep -Milliseconds 500
    }
    throw 'MiaoDesk file search service did not become reachable after 60 seconds.'
}

if (-not (Test-Path $CompleteMarker -PathType Leaf) -or -not (Test-Path $ManifestPath -PathType Leaf)) {
    throw 'MiaoDesk x64 RuntimeBundle is not available.'
}
$manifest = Get-Content $ManifestPath -Raw | ConvertFrom-Json
if ($manifest.architecture -ne 'x64' -or [int]$manifest.schema -lt 2) { throw 'MiaoDesk x64 RuntimeBundle architecture/schema mismatch.' }
if (-not $manifest.pi -or -not $manifest.deepseekHarness) { throw 'MiaoDesk x64 RuntimeBundle is missing Pi/DSH runtime metadata.' }
if ([string]::IsNullOrWhiteSpace([string]$manifest.pi.dependencyLockSha256) -or
    [string]::IsNullOrWhiteSpace([string]$manifest.deepseekHarness.dependencyLockSha256)) {
    throw 'MiaoDesk x64 RuntimeBundle is missing frozen Pi/DSH dependency lock hashes.'
}

$nodeArchive = Resolve-BundleFile ([string]$manifest.node.archive)
$harnessArchive = Resolve-BundleFile ([string]$manifest.deepseekHarness.archive)
$gozArchive = Resolve-BundleFile ([string]$manifest.goz.archive)
$piArchive = Resolve-BundleFile ([string]$manifest.pi.archive)
Assert-BundleHash $nodeArchive ([string]$manifest.node.sha256)
Assert-BundleHash $harnessArchive ([string]$manifest.deepseekHarness.sha256)
Assert-BundleHash $gozArchive ([string]$manifest.goz.sha256)
Assert-BundleHash $piArchive ([string]$manifest.pi.sha256)

$RuntimeDir = Join-Path $DeployDir 'Runtime'
$NodeDir = Join-Path $RuntimeDir 'Node'
$PiDir = Join-Path $DeployDir 'Pi'
$GozDir = Join-Path $DeployDir 'Goz'
$NodeExe = Join-Path $NodeDir 'node.exe'
$DshBin = Join-Path $NodeDir 'node_modules\@deepseek-ai\dsh\lib\bin.js'
$DshLock = Join-Path $NodeDir 'package-lock.json'
$PiCli = Join-Path $PiDir 'node_modules\@earendil-works\pi-coding-agent\dist\cli.js'
$PiLock = Join-Path $PiDir 'package-lock.json'
$GozExe = Join-Path $GozDir 'goz.exe'
$GozDaemon = Join-Path $GozDir 'gozd.exe'
$DeployManifestHash = Join-Path $RuntimeDir 'runtime-manifest.sha256'
$sourceManifestHash = Sha256 $ManifestPath

$ready = (Test-Path $DeployManifestHash -PathType Leaf) -and (Test-Path $NodeExe -PathType Leaf) -and
         (Test-Path $DshBin -PathType Leaf) -and (Test-Path $DshLock -PathType Leaf) -and
         (Test-Path $PiCli -PathType Leaf) -and (Test-Path $PiLock -PathType Leaf) -and
         (Test-Path $GozExe -PathType Leaf) -and (Test-Path $GozDaemon -PathType Leaf) -and
         ((Get-Content $DeployManifestHash -Raw).Trim().ToLowerInvariant() -eq $sourceManifestHash)
if ($ready) {
    Assert-BundleHash $DshLock ([string]$manifest.deepseekHarness.dependencyLockSha256)
    Assert-BundleHash $PiLock ([string]$manifest.pi.dependencyLockSha256)
    Ensure-IndexService $GozExe $GozDaemon
    Write-Host "MiaoDesk x64 RuntimeBundle is ready: $DeployDir" -ForegroundColor Green
    exit 0
}

Step 'Installing pinned MiaoDesk x64 RuntimeBundle (offline)'
Stop-OwnedProcesses
New-Item -ItemType Directory -Force -Path $RuntimeDir | Out-Null

$gozTemp = Join-Path $env:TEMP ('MiaoDesk-x64-Goz-' + [guid]::NewGuid().ToString('N'))
try {
    Expand-BundleArchive $gozArchive $gozTemp
    $newGoz = Get-ChildItem $gozTemp -Filter goz.exe -File -Recurse | Select-Object -First 1
    $newGozd = Get-ChildItem $gozTemp -Filter gozd.exe -File -Recurse | Select-Object -First 1
    if (-not $newGoz -or -not $newGozd) { throw 'MiaoDesk x64 goz archive is incomplete.' }
    Remove-Item $GozDir -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path $GozDir | Out-Null
    Copy-Item (Join-Path $newGoz.Directory.FullName '*') $GozDir -Recurse -Force
    if (-not (Test-Path $GozDaemon -PathType Leaf) -and $newGozd.Directory.FullName -ne $newGoz.Directory.FullName) {
        Copy-Item $newGozd.FullName $GozDaemon -Force
    }
} finally { Remove-Item $gozTemp -Recurse -Force -ErrorAction SilentlyContinue }

Remove-Item $NodeDir,$PiDir -Recurse -Force -ErrorAction SilentlyContinue

$nodeTemp = Join-Path $env:TEMP ('MiaoDesk-x64-Node-' + [guid]::NewGuid().ToString('N'))
try {
    Expand-BundleArchive $nodeArchive $nodeTemp
    $root = Get-ChildItem $nodeTemp -Directory | Where-Object { Test-Path (Join-Path $_.FullName 'node.exe') -PathType Leaf } | Select-Object -First 1
    if (-not $root) { throw 'MiaoDesk bundled x64 Node archive is invalid.' }
    New-Item -ItemType Directory -Force -Path $NodeDir | Out-Null
    Copy-Item (Join-Path $root.FullName '*') $NodeDir -Recurse -Force
} finally { Remove-Item $nodeTemp -Recurse -Force -ErrorAction SilentlyContinue }

Expand-BundleArchive $harnessArchive $NodeDir
Assert-File $NodeExe 'shared bundled Node runtime'
Assert-File $DshBin 'DeepSeek Harness runtime'
Assert-File $DshLock 'DeepSeek Harness dependency lock'
Assert-BundleHash $DshLock ([string]$manifest.deepseekHarness.dependencyLockSha256)
& $NodeExe $DshBin --help | Out-Host
if ($LASTEXITCODE -ne 0) { throw 'MiaoDesk x64 DeepSeek Harness runtime failed to start.' }

Expand-BundleArchive $piArchive $PiDir
Assert-File $PiCli 'Pi Agent runtime'
Assert-File $PiLock 'Pi dependency lock'
Assert-BundleHash $PiLock ([string]$manifest.pi.dependencyLockSha256)
& $NodeExe $PiCli --version | Out-Host
if ($LASTEXITCODE -ne 0) { throw 'MiaoDesk x64 Pi Agent runtime failed to start.' }

Assert-File $GozExe 'goz client'
Assert-File $GozDaemon 'goz daemon'
Copy-Item $ManifestPath (Join-Path $RuntimeDir 'runtime-manifest.json') -Force
Set-Content $DeployManifestHash -Value $sourceManifestHash -Encoding ASCII
Ensure-IndexService $GozExe $GozDaemon

Write-Host 'MiaoDesk x64 RuntimeBundle is ready.' -ForegroundColor Green
Write-Host "Shared Node:          $NodeExe" -ForegroundColor DarkGray
Write-Host "Pi Agent:             $PiCli" -ForegroundColor DarkGray
Write-Host "DeepSeek Harness:     $DshBin" -ForegroundColor DarkGray
Write-Host "File Search:          $GozExe" -ForegroundColor DarkGray
Write-Host 'Pi and DSH dependency trees were verified against their frozen package-lock hashes.' -ForegroundColor Green
Write-Host 'No third-party network download or system Node installation was performed.' -ForegroundColor Green
