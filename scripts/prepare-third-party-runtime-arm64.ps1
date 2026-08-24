param(
    [string]$DeployDir = (Join-Path $env:LOCALAPPDATA 'TuringDesk\NativeTest'),
    [switch]$SkipGozServiceInstall
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$RepoRoot = Split-Path $PSScriptRoot -Parent
$BundleRoot = Join-Path $RepoRoot 'runtime\arm64'
$ManifestPath = Join-Path $BundleRoot 'runtime-manifest.json'
$CompleteMarker = Join-Path $BundleRoot '.complete'

function Step([string]$Text) { Write-Host "`n==> $Text" -ForegroundColor Cyan }
function Sha256([string]$Path) { (Get-FileHash -Algorithm SHA256 -Path $Path).Hash.ToLowerInvariant() }
function Resolve-BundleFile([string]$RelativePath) {
    $path = Join-Path $BundleRoot ($RelativePath -replace '/', '\')
    if (-not (Test-Path $path -PathType Leaf)) { throw "TuringDesk RuntimeBundle file is missing: $path" }
    $path
}
function Assert-BundleHash([string]$Path, [string]$Expected) {
    $actual = Sha256 $Path
    if ([string]::IsNullOrWhiteSpace($Expected) -or $actual -ne $Expected.ToLowerInvariant()) {
        throw "TuringDesk RuntimeBundle integrity check failed: $Path`nExpected: $Expected`nActual:   $actual"
    }
}
function Assert-File([string]$Path, [string]$Label) {
    if (-not (Test-Path $Path -PathType Leaf)) { throw ("TuringDesk RuntimeBundle is missing {0}: {1}" -f $Label, $Path) }
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
    $names = @('TuringDesk.exe','TuringDeskWallpaper.exe','TuringDeskHarness.exe','node.exe','goz.exe','gozd.exe')
    try {
        foreach ($p in @(Get-CimInstance Win32_Process -ErrorAction Stop)) {
            if ($names -notcontains [string]$p.Name) { continue }
            $exe = [string]$p.ExecutablePath
            if (-not $exe -or -not (Test-InDeploy $exe)) { continue }
            & taskkill.exe /PID $p.ProcessId /T /F 2>$null | Out-Null
        }
    } catch { Write-Host "TuringDesk process scan warning: $($_.Exception.Message)" -ForegroundColor DarkYellow }
    Start-Sleep -Milliseconds 250
}
function Expand-BundleArchive([string]$Archive, [string]$Destination) {
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    & tar.exe -xf $Archive -C $Destination
    if ($LASTEXITCODE -ne 0) { throw "Failed to extract TuringDesk RuntimeBundle archive: $Archive" }
}
function Invoke-ElevatedIndexService([string]$Exe, [string]$Arguments) {
    $p = Start-Process -FilePath $Exe -ArgumentList $Arguments -Verb RunAs -Wait -PassThru
    if (-not $p -or $p.ExitCode -ne 0) { throw "TuringDesk file index service operation failed: $Arguments" }
}
function Probe([string]$Exe, [string[]]$Arguments) {
    $out = Join-Path $env:TEMP ('td-probe-o-' + [guid]::NewGuid().ToString('N'))
    $err = Join-Path $env:TEMP ('td-probe-e-' + [guid]::NewGuid().ToString('N'))
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
        if ((Probe $IndexExe @('--status')).ExitCode -eq 0) { Write-Host 'TuringDesk file search is ready.' -ForegroundColor Green; return }
        Start-Sleep -Milliseconds 500
    }
    throw 'TuringDesk file search service did not become reachable after 60 seconds.'
}

if (-not (Test-Path $CompleteMarker -PathType Leaf) -or -not (Test-Path $ManifestPath -PathType Leaf)) {
    throw 'TuringDesk ARM64 RuntimeBundle is not available.'
}
$manifest = Get-Content $ManifestPath -Raw | ConvertFrom-Json
if ($manifest.architecture -ne 'arm64' -or [int]$manifest.schema -lt 2) { throw 'TuringDesk RuntimeBundle architecture/schema mismatch.' }
if (-not $manifest.pi) { throw 'TuringDesk RuntimeBundle is missing the Agent runtime component.' }

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
$PiCli = Join-Path $PiDir 'node_modules\@earendil-works\pi-coding-agent\dist\cli.js'
$GozExe = Join-Path $GozDir 'goz.exe'
$GozDaemon = Join-Path $GozDir 'gozd.exe'
$DeployManifestHash = Join-Path $RuntimeDir 'runtime-manifest.sha256'
$sourceManifestHash = Sha256 $ManifestPath

$ready = (Test-Path $DeployManifestHash -PathType Leaf) -and (Test-Path $NodeExe -PathType Leaf) -and
         (Test-Path $DshBin -PathType Leaf) -and (Test-Path $PiCli -PathType Leaf) -and
         (Test-Path $GozExe -PathType Leaf) -and (Test-Path $GozDaemon -PathType Leaf) -and
         ((Get-Content $DeployManifestHash -Raw).Trim().ToLowerInvariant() -eq $sourceManifestHash)
if ($ready) {
    Ensure-IndexService $GozExe $GozDaemon
    Write-Host "TuringDesk RuntimeBundle is ready: $DeployDir" -ForegroundColor Green
    exit 0
}

Step 'Installing pinned TuringDesk ARM64 RuntimeBundle (offline)'
Stop-OwnedProcesses
New-Item -ItemType Directory -Force -Path $RuntimeDir | Out-Null

# The file index daemon is a Windows service: only replace its binaries when content changed.
$gozTemp = Join-Path $env:TEMP ('TuringDesk-Index-' + [guid]::NewGuid().ToString('N'))
try {
    Expand-BundleArchive $gozArchive $gozTemp
    $newGoz = Get-ChildItem $gozTemp -Filter goz.exe -File -Recurse | Select-Object -First 1
    $newGozd = Get-ChildItem $gozTemp -Filter gozd.exe -File -Recurse | Select-Object -First 1
    if (-not $newGoz -or -not $newGozd) { throw 'TuringDesk file index archive is incomplete' }
    $replace = $true
    if ((Test-Path $GozExe) -and (Test-Path $GozDaemon)) {
        $replace = (Sha256 $GozExe) -ne (Sha256 $newGoz.FullName) -or (Sha256 $GozDaemon) -ne (Sha256 $newGozd.FullName)
    }
    if ($replace) {
        if ((Test-Path $GozDaemon) -and -not $SkipGozServiceInstall) { try { Invoke-ElevatedIndexService $GozDaemon 'uninstall' } catch {} }
        Remove-Item $GozDir -Recurse -Force -ErrorAction SilentlyContinue
        New-Item -ItemType Directory -Force -Path $GozDir | Out-Null
        Copy-Item (Join-Path $gozTemp '*') $GozDir -Recurse -Force
    }
} finally { Remove-Item $gozTemp -Recurse -Force -ErrorAction SilentlyContinue }

Remove-Item $NodeDir,$PiDir -Recurse -Force -ErrorAction SilentlyContinue

$nodeTemp = Join-Path $env:TEMP ('TuringDesk-Node-' + [guid]::NewGuid().ToString('N'))
try {
    Expand-BundleArchive $nodeArchive $nodeTemp
    $root = Get-ChildItem $nodeTemp -Directory | Select-Object -First 1
    if (-not $root -or -not (Test-Path (Join-Path $root.FullName 'node.exe'))) { throw 'TuringDesk bundled runtime archive is invalid' }
    New-Item -ItemType Directory -Force -Path $NodeDir | Out-Null
    Copy-Item (Join-Path $root.FullName '*') $NodeDir -Recurse -Force
} finally { Remove-Item $nodeTemp -Recurse -Force -ErrorAction SilentlyContinue }

# Advanced Workbench keeps its own dependency tree inside the portable runtime folder.
Expand-BundleArchive $harnessArchive $NodeDir
Assert-File $NodeExe 'bundled AI runtime'
Assert-File $DshBin 'advanced workbench runtime'
& $NodeExe $DshBin --help | Out-Host
if ($LASTEXITCODE -ne 0) { throw 'TuringDesk Advanced Workbench runtime failed to start' }

# Agent runtime uses an isolated dependency tree to avoid dependency collisions.
Expand-BundleArchive $piArchive $PiDir
Assert-File $PiCli 'agent runtime'
& $NodeExe $PiCli --version | Out-Host
if ($LASTEXITCODE -ne 0) { throw 'TuringDesk Agent runtime failed to start' }

Copy-Item $ManifestPath (Join-Path $RuntimeDir 'runtime-manifest.json') -Force
Set-Content $DeployManifestHash -Value $sourceManifestHash -Encoding ASCII
Ensure-IndexService $GozExe $GozDaemon

Write-Host 'TuringDesk ARM64 RuntimeBundle is ready.' -ForegroundColor Green
Write-Host "AI Runtime:          $NodeExe" -ForegroundColor DarkGray
Write-Host "Agent Runtime:       $PiCli" -ForegroundColor DarkGray
Write-Host "Advanced Workbench:  $DshBin" -ForegroundColor DarkGray
Write-Host "File Search:          $GozExe" -ForegroundColor DarkGray
Write-Host 'No third-party network download or system Node installation was performed.' -ForegroundColor Green
