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
    if (-not (Test-Path $path -PathType Leaf)) { throw "Vendored runtime file is missing: $path" }
    $path
}
function Assert-BundleHash([string]$Path, [string]$Expected) {
    $actual = Sha256 $Path
    if ([string]::IsNullOrWhiteSpace($Expected) -or $actual -ne $Expected.ToLowerInvariant()) {
        throw "Vendored runtime integrity check failed: $Path`nExpected: $Expected`nActual:   $actual"
    }
}
function Assert-File([string]$Path, [string]$Label) {
    if (-not (Test-Path $Path -PathType Leaf)) { throw "Full Codex package is missing $Label: $Path" }
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
    $names = @('TuringDesk.exe','TuringDeskWallpaper.exe','TuringDeskHarness.exe','node.exe','codex.exe','codex-relay.exe','codex-command-runner.exe','codex-windows-sandbox-setup.exe','codex-code-mode-host.exe')
    try {
        foreach ($p in @(Get-CimInstance Win32_Process -ErrorAction Stop)) {
            if ($names -notcontains [string]$p.Name) { continue }
            $exe = [string]$p.ExecutablePath
            if (-not $exe -or -not (Test-InDeploy $exe)) { continue }
            & taskkill.exe /PID $p.ProcessId /T /F 2>$null | Out-Null
        }
    } catch { Write-Host "Process scan warning: $($_.Exception.Message)" -ForegroundColor DarkYellow }
    Start-Sleep -Milliseconds 250
}
function Expand-BundleArchive([string]$Archive, [string]$Destination) {
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    if ($Archive.EndsWith('.tar.zst', [StringComparison]::OrdinalIgnoreCase)) {
        if (-not (Test-Path $script:NodeExe -PathType Leaf)) { throw 'Node 24 is required to materialize Codex tar.zst payload' }
        $tarPath = Join-Path $env:TEMP ('td-codex-' + [guid]::NewGuid().ToString('N') + '.tar')
        $js = "const fs=require('node:fs');const z=require('node:zlib');const [s,d]=process.argv.slice(1);fs.writeFileSync(d,z.zstdDecompressSync(fs.readFileSync(s)));"
        try {
            & $script:NodeExe -e $js $Archive $tarPath
            if ($LASTEXITCODE -ne 0 -or -not (Test-Path $tarPath -PathType Leaf)) { throw "Failed to materialize $Archive" }
            & tar.exe -xf $tarPath -C $Destination
            if ($LASTEXITCODE -ne 0) { throw "Failed to extract $Archive" }
        }
        finally { Remove-Item $tarPath -Force -ErrorAction SilentlyContinue }
        return
    }
    if ($Archive.EndsWith('.zst', [StringComparison]::OrdinalIgnoreCase)) {
        if (-not (Test-Path $script:NodeExe -PathType Leaf)) { throw 'Node 24 is required to materialize Zstd payload' }
        $out = Join-Path $Destination ([IO.Path]::GetFileNameWithoutExtension($Archive))
        $js = "const fs=require('node:fs');const z=require('node:zlib');const [s,d]=process.argv.slice(1);fs.writeFileSync(d,z.zstdDecompressSync(fs.readFileSync(s)));"
        & $script:NodeExe -e $js $Archive $out
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path $out -PathType Leaf)) { throw "Failed to materialize $Archive" }
        return
    }
    & tar.exe -xf $Archive -C $Destination
    if ($LASTEXITCODE -ne 0) { throw "Failed to extract $Archive" }
}
function Invoke-ElevatedGoz([string]$Exe, [string]$Arguments) {
    $p = Start-Process -FilePath $Exe -ArgumentList $Arguments -Verb RunAs -Wait -PassThru
    if (-not $p -or $p.ExitCode -ne 0) { throw "Elevated goz operation failed: $Arguments" }
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
function Ensure-GozService([string]$GozExe,[string]$GozDaemon) {
    if ($SkipGozServiceInstall) { return }
    $status = Probe $GozDaemon @('status')
    if ($status.ExitCode -ne 0 -or $status.Text -notmatch '(?i)Running') { Invoke-ElevatedGoz $GozDaemon 'install' }
    for ($i=0; $i -lt 120; $i++) {
        if ((Probe $GozExe @('--status')).ExitCode -eq 0) { Write-Host 'goz index service is reachable.' -ForegroundColor Green; return }
        Start-Sleep -Milliseconds 500
    }
    throw 'goz service did not become reachable after 60 seconds.'
}

if (-not (Test-Path $CompleteMarker -PathType Leaf) -or -not (Test-Path $ManifestPath -PathType Leaf)) {
    throw 'TuringDesk ARM64 RuntimeBundle is not vendored yet.'
}
$manifest = Get-Content $ManifestPath -Raw | ConvertFrom-Json
if ($manifest.architecture -ne 'arm64' -or [int]$manifest.schema -lt 2) { throw 'RuntimeBundle is not the ARM64 goz/Codex bundle' }

$nodeArchive = Resolve-BundleFile ([string]$manifest.node.archive)
$harnessArchive = Resolve-BundleFile ([string]$manifest.deepseekHarness.archive)
$gozArchive = Resolve-BundleFile ([string]$manifest.goz.archive)
$relayArchive = Resolve-BundleFile ([string]$manifest.codexRelay.archive)
$codexArchive = Resolve-BundleFile ([string]$manifest.codex.archive)
Assert-BundleHash $nodeArchive ([string]$manifest.node.sha256)
Assert-BundleHash $harnessArchive ([string]$manifest.deepseekHarness.sha256)
Assert-BundleHash $gozArchive ([string]$manifest.goz.sha256)
Assert-BundleHash $relayArchive ([string]$manifest.codexRelay.sha256)
Assert-BundleHash $codexArchive ([string]$manifest.codex.sha256)

$RuntimeDir = Join-Path $DeployDir 'Runtime'
$NodeDir = Join-Path $RuntimeDir 'Node'
$GozDir = Join-Path $DeployDir 'Goz'
$RelayDir = Join-Path $DeployDir 'CodexRelay'
$CodexDir = Join-Path $DeployDir 'Codex'
$NodeExe = Join-Path $NodeDir 'node.exe'
$DshBin = Join-Path $NodeDir 'node_modules\@deepseek-ai\dsh\lib\bin.js'
$GozExe = Join-Path $GozDir 'goz.exe'
$GozDaemon = Join-Path $GozDir 'gozd.exe'
$RelayExe = Join-Path $RelayDir 'codex-relay.exe'
$CodexExe = Join-Path $CodexDir 'codex.exe'
$CodexPackage = Join-Path $CodexDir 'codex-package.json'
$CodexCommandRunner = Join-Path $CodexDir 'codex-resources\codex-command-runner.exe'
$CodexSandboxSetup = Join-Path $CodexDir 'codex-resources\codex-windows-sandbox-setup.exe'
$CodexCodeModeHost = Join-Path $CodexDir 'codex-code-mode-host.exe'
$CodexRg = Join-Path $CodexDir 'codex-path\rg.exe'
$DeployManifestHash = Join-Path $RuntimeDir 'runtime-manifest.sha256'
$sourceManifestHash = Sha256 $ManifestPath

$ready = (Test-Path $DeployManifestHash -PathType Leaf) -and (Test-Path $NodeExe -PathType Leaf) -and
         (Test-Path $DshBin -PathType Leaf) -and (Test-Path $GozExe -PathType Leaf) -and
         (Test-Path $GozDaemon -PathType Leaf) -and (Test-Path $RelayExe -PathType Leaf) -and
         (Test-Path $CodexExe -PathType Leaf) -and (Test-Path $CodexPackage -PathType Leaf) -and
         (Test-Path $CodexCommandRunner -PathType Leaf) -and (Test-Path $CodexSandboxSetup -PathType Leaf) -and
         (Test-Path $CodexCodeModeHost -PathType Leaf) -and (Test-Path $CodexRg -PathType Leaf) -and
         ((Get-Content $DeployManifestHash -Raw).Trim().ToLowerInvariant() -eq $sourceManifestHash)
if ($ready) { Ensure-GozService $GozExe $GozDaemon; Write-Host "RuntimeBundle ready: $DeployDir" -ForegroundColor Green; exit 0 }

Step 'Installing repository-vendored ARM64 RuntimeBundle (offline)'
Stop-OwnedProcesses
New-Item -ItemType Directory -Force -Path $RuntimeDir | Out-Null

# goz is a Windows service: only replace its binaries when content changed.
$gozTemp = Join-Path $env:TEMP ('TuringDesk-Goz-' + [guid]::NewGuid().ToString('N'))
try {
    Expand-BundleArchive $gozArchive $gozTemp
    $newGoz = Get-ChildItem $gozTemp -Filter goz.exe -File -Recurse | Select-Object -First 1
    $newGozd = Get-ChildItem $gozTemp -Filter gozd.exe -File -Recurse | Select-Object -First 1
    if (-not $newGoz -or -not $newGozd) { throw 'Vendored goz archive is incomplete' }
    $replace = $true
    if ((Test-Path $GozExe) -and (Test-Path $GozDaemon)) {
        $replace = (Sha256 $GozExe) -ne (Sha256 $newGoz.FullName) -or (Sha256 $GozDaemon) -ne (Sha256 $newGozd.FullName)
    }
    if ($replace) {
        if ((Test-Path $GozDaemon) -and -not $SkipGozServiceInstall) { try { Invoke-ElevatedGoz $GozDaemon 'uninstall' } catch {} }
        Remove-Item $GozDir -Recurse -Force -ErrorAction SilentlyContinue
        New-Item -ItemType Directory -Force -Path $GozDir | Out-Null
        Copy-Item (Join-Path $gozTemp '*') $GozDir -Recurse -Force
    }
} finally { Remove-Item $gozTemp -Recurse -Force -ErrorAction SilentlyContinue }

Remove-Item $NodeDir,$RelayDir,$CodexDir -Recurse -Force -ErrorAction SilentlyContinue

$nodeTemp = Join-Path $env:TEMP ('TuringDesk-Node-' + [guid]::NewGuid().ToString('N'))
try {
    Expand-BundleArchive $nodeArchive $nodeTemp
    $root = Get-ChildItem $nodeTemp -Directory | Select-Object -First 1
    if (-not $root -or -not (Test-Path (Join-Path $root.FullName 'node.exe'))) { throw 'Vendored Node archive is invalid' }
    New-Item -ItemType Directory -Force -Path $NodeDir | Out-Null
    Copy-Item (Join-Path $root.FullName '*') $NodeDir -Recurse -Force
} finally { Remove-Item $nodeTemp -Recurse -Force -ErrorAction SilentlyContinue }
Expand-BundleArchive $harnessArchive $NodeDir
if (-not (Test-Path $NodeExe) -or -not (Test-Path $DshBin)) { throw 'Bundled Harness runtime is incomplete' }
& $NodeExe $DshBin --help | Out-Host
if ($LASTEXITCODE -ne 0) { throw 'Bundled DeepSeek Harness CLI failed to start' }

$relayTemp = Join-Path $env:TEMP ('TuringDesk-Relay-' + [guid]::NewGuid().ToString('N'))
try {
    Expand-BundleArchive $relayArchive $relayTemp
    $relay = Get-ChildItem $relayTemp -Filter codex-relay.exe -File -Recurse | Select-Object -First 1
    if (-not $relay) { throw 'Vendored codex-relay archive is incomplete' }
    New-Item -ItemType Directory -Force -Path $RelayDir | Out-Null
    Copy-Item $relay.FullName $RelayExe -Force
} finally { Remove-Item $relayTemp -Recurse -Force -ErrorAction SilentlyContinue }
& $RelayExe --help | Out-Host
if ($LASTEXITCODE -ne 0) { throw 'Bundled codex-relay failed to execute' }

$codexTemp = Join-Path $env:TEMP ('TuringDesk-Codex-' + [guid]::NewGuid().ToString('N'))
try {
    Expand-BundleArchive $codexArchive $codexTemp
    $packageRoot = $null
    if (Test-Path (Join-Path $codexTemp 'codex-package.json') -PathType Leaf) {
        $packageRoot = Get-Item $codexTemp
    } else {
        $packageRoot = Get-ChildItem $codexTemp -Directory | Where-Object { Test-Path (Join-Path $_.FullName 'codex-package.json') -PathType Leaf } | Select-Object -First 1
    }
    if (-not $packageRoot) { throw 'Vendored Codex package is missing codex-package.json' }

    $packageCodex = Join-Path $packageRoot.FullName 'bin\codex.exe'
    $packageCodeModeHost = Join-Path $packageRoot.FullName 'bin\codex-code-mode-host.exe'
    $packageCommandRunner = Join-Path $packageRoot.FullName 'codex-resources\codex-command-runner.exe'
    $packageSandboxSetup = Join-Path $packageRoot.FullName 'codex-resources\codex-windows-sandbox-setup.exe'
    $packageRg = Join-Path $packageRoot.FullName 'codex-path\rg.exe'
    Assert-File $packageCodex 'bin/codex.exe'
    Assert-File $packageCodeModeHost 'bin/codex-code-mode-host.exe'
    Assert-File $packageCommandRunner 'codex-resources/codex-command-runner.exe'
    Assert-File $packageSandboxSetup 'codex-resources/codex-windows-sandbox-setup.exe'
    Assert-File $packageRg 'codex-path/rg.exe'

    New-Item -ItemType Directory -Force -Path $CodexDir | Out-Null
    Copy-Item (Join-Path $packageRoot.FullName '*') $CodexDir -Recurse -Force

    # Compatibility projection: current TuringDesk launches Codex\codex.exe. Keeping
    # codex-resources and codex-package.json beside it preserves the official helper lookup.
    Copy-Item (Join-Path $CodexDir 'bin\codex.exe') $CodexExe -Force
    Copy-Item (Join-Path $CodexDir 'bin\codex-code-mode-host.exe') $CodexCodeModeHost -Force
} finally { Remove-Item $codexTemp -Recurse -Force -ErrorAction SilentlyContinue }

Assert-File $CodexPackage 'codex-package.json'
Assert-File $CodexCommandRunner 'codex-command-runner.exe'
Assert-File $CodexSandboxSetup 'codex-windows-sandbox-setup.exe'
Assert-File $CodexCodeModeHost 'codex-code-mode-host.exe'
Assert-File $CodexRg 'rg.exe'
& $CodexExe --version | Out-Host
if ($LASTEXITCODE -ne 0) { throw 'Bundled Codex CLI failed to execute' }
& $CodexExe app-server --help | Out-Host
if ($LASTEXITCODE -ne 0) { throw 'Bundled Codex CLI app-server is unavailable' }
& $CodexRg --version | Out-Host
if ($LASTEXITCODE -ne 0) { throw 'Bundled Codex ripgrep failed to execute' }
& $CodexCommandRunner --help | Out-Host
if ($LASTEXITCODE -ne 0) { throw 'Bundled Codex command runner failed to execute' }
& $CodexSandboxSetup --help | Out-Host
if ($LASTEXITCODE -ne 0) { throw 'Bundled Codex Windows sandbox setup failed to execute' }

Copy-Item $ManifestPath (Join-Path $RuntimeDir 'runtime-manifest.json') -Force
Set-Content $DeployManifestHash -Value $sourceManifestHash -Encoding ASCII
Ensure-GozService $GozExe $GozDaemon
Write-Host 'Repository-vendored ARM64 RuntimeBundle ready.' -ForegroundColor Green
Write-Host "Node:    $NodeExe" -ForegroundColor DarkGray
Write-Host "Harness: $DshBin" -ForegroundColor DarkGray
Write-Host "goz:     $GozExe" -ForegroundColor DarkGray
Write-Host "gozd:    $GozDaemon" -ForegroundColor DarkGray
Write-Host "Relay:   $RelayExe" -ForegroundColor DarkGray
Write-Host "Codex:   $CodexExe" -ForegroundColor DarkGray
Write-Host "Runner:  $CodexCommandRunner" -ForegroundColor DarkGray
Write-Host "Sandbox: $CodexSandboxSetup" -ForegroundColor DarkGray
Write-Host 'No third-party network download or system Node installation was performed.' -ForegroundColor Green
