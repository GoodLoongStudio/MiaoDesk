param(
    [string]$PreviewRoot = (Join-Path $env:LOCALAPPDATA 'TuringDesk\DevPreview'),
    [int]$WaitSeconds = 8,
    [switch]$Relaunch,
    [switch]$RunSelfTests
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

function Section([string]$Text) {
    Write-Host "`n==> $Text" -ForegroundColor Cyan
}

function Pass([string]$Text) {
    Write-Host "[PASS] $Text" -ForegroundColor Green
}

function Warn([string]$Text) {
    Write-Host "[WARN] $Text" -ForegroundColor Yellow
}

function Fail([string]$Text) {
    Write-Host "[FAIL] $Text" -ForegroundColor Red
}

function Get-PeMachine([string]$Path) {
    $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
    try {
        $reader = New-Object IO.BinaryReader($stream)
        if ($reader.ReadUInt16() -ne 0x5A4D) { throw "Not a PE image: $Path" }
        $stream.Position = 0x3C
        $peOffset = $reader.ReadInt32()
        if ($peOffset -lt 0 -or $peOffset -gt ($stream.Length - 6)) { throw "Invalid PE header: $Path" }
        $stream.Position = $peOffset
        if ($reader.ReadUInt32() -ne 0x00004550) { throw "Invalid PE signature: $Path" }
        return $reader.ReadUInt16()
    }
    finally {
        $stream.Dispose()
    }
}

function Invoke-SelfTest([string]$Exe, [string]$Name, [string[]]$Arguments) {
    try {
        $process = Start-Process -FilePath $Exe -ArgumentList $Arguments -Wait -PassThru -NoNewWindow
        if ($process.ExitCode -eq 0) {
            Pass "$Name $($Arguments -join ' ')"
            return $true
        }
        Fail "$Name $($Arguments -join ' ') exited $($process.ExitCode)"
        return $false
    }
    catch {
        Fail "$Name self-test could not run: $($_.Exception.Message)"
        return $false
    }
}

function Get-Marker([string]$Name) {
    $path = Join-Path $PreviewRoot $Name
    if (-not (Test-Path $path -PathType Leaf)) { return $null }
    return ([string](Get-Content $path -Raw)).Trim()
}

Section 'ARM64 host preflight'
$processor = Get-CimInstance Win32_Processor | Select-Object -First 1
$architecture = if ($processor) { [int]$processor.Architecture } else { -1 }
if ($architecture -ne 12) {
    throw "This is not a native ARM64 Windows host. Win32_Processor.Architecture=$architecture"
}
Pass "Native Windows ARM64 host detected: $($processor.Name)"
Write-Host "Windows: $([Environment]::OSVersion.VersionString)"
Write-Host "PowerShell process architecture: $env:PROCESSOR_ARCHITECTURE"

Section 'Preview package identity'
if (-not (Test-Path $PreviewRoot -PathType Container)) {
    throw "Preview directory does not exist: $PreviewRoot. Run scripts\download-arm64-preview.ps1 first."
}
$buildSha = Get-Marker 'preview-build-sha.txt'
$checkoutSha = Get-Marker 'preview-checkout-sha.txt'
if ($buildSha) { Write-Host "Preview binary SHA: $buildSha" }
if ($checkoutSha) { Write-Host "Checkout SHA:       $checkoutSha" }
if ($buildSha -and $checkoutSha -and $buildSha -ne $checkoutSha) {
    Warn 'Preview uses a validated ancestor binary. This is allowed only when the downloader found no binary-impacting changes.'
}

$required = @('TuringDesk.exe', 'TuringDeskWallpaper.exe', 'TuringDeskHarness.exe')
$binaryResults = @()
foreach ($name in $required) {
    $path = Join-Path $PreviewRoot $name
    if (-not (Test-Path $path -PathType Leaf)) { throw "Missing preview binary: $path" }
    $machine = Get-PeMachine $path
    $isArm64 = $machine -eq 0xAA64
    $binaryResults += [pscustomobject]@{ Name = $name; Machine = ('0x{0:X4}' -f $machine); Arm64 = $isArm64 }
    if (-not $isArm64) { throw "$name is not native ARM64. PE machine=0x$('{0:X4}' -f $machine)" }
    Pass "$name is native ARM64 (PE 0xAA64)"
}

Section 'Display topology'
Add-Type -AssemblyName System.Windows.Forms
$screens = @([System.Windows.Forms.Screen]::AllScreens)
Write-Host "Display count: $($screens.Count)"
$displayResults = @()
foreach ($screen in $screens) {
    $bounds = $screen.Bounds
    $displayResults += [pscustomobject]@{
        Device = $screen.DeviceName
        Primary = $screen.Primary
        X = $bounds.X
        Y = $bounds.Y
        Width = $bounds.Width
        Height = $bounds.Height
    }
    Write-Host ("  {0} primary={1} bounds={2},{3} {4}x{5}" -f $screen.DeviceName, $screen.Primary, $bounds.X, $bounds.Y, $bounds.Width, $bounds.Height)
}

if ($RunSelfTests) {
    Section 'Native self-tests on the physical ARM64 host'
    Invoke-SelfTest (Join-Path $PreviewRoot 'TuringDesk.exe') 'TuringDesk' @('--self-test') | Out-Null
    Invoke-SelfTest (Join-Path $PreviewRoot 'TuringDeskWallpaper.exe') 'TuringDeskWallpaper' @('--self-test') | Out-Null
    Invoke-SelfTest (Join-Path $PreviewRoot 'TuringDeskHarness.exe') 'TuringDeskHarness' @('--self-test') | Out-Null
    if (Test-Path (Join-Path $PreviewRoot 'Runtime') -PathType Container) {
        Invoke-SelfTest (Join-Path $PreviewRoot 'TuringDeskHarness.exe') 'TuringDeskHarness' @('--harness-smoke-test') | Out-Null
    }
    else {
        Warn 'Runtime bundle is not linked into this preview; Harness smoke test was skipped.'
    }
}

Section 'Live process check'
if ($Relaunch) {
    foreach ($name in @('TuringDesk', 'TuringDeskWallpaper', 'TuringDeskHarness')) {
        Get-Process $name -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    }
    Start-Sleep -Milliseconds 600
    Start-Process -FilePath (Join-Path $PreviewRoot 'TuringDesk.exe') -WorkingDirectory $PreviewRoot
}
elseif (-not (Get-Process TuringDesk -ErrorAction SilentlyContinue)) {
    Warn 'TuringDesk is not running; starting the preview for live checks.'
    Start-Process -FilePath (Join-Path $PreviewRoot 'TuringDesk.exe') -WorkingDirectory $PreviewRoot
}

Start-Sleep -Seconds ([Math]::Max(2, $WaitSeconds))
$processResults = @()
foreach ($name in @('TuringDesk', 'TuringDeskWallpaper', 'TuringDeskHarness')) {
    $items = @(Get-Process $name -ErrorAction SilentlyContinue)
    $running = $items.Count -gt 0
    $processResults += [pscustomobject]@{ Name = $name; Running = $running; Count = $items.Count }
    if ($running) { Pass "$name process is alive (count=$($items.Count))" }
    elseif ($name -eq 'TuringDeskHarness') { Warn 'TuringDeskHarness is not running. This can be expected while the advanced workbench is not opened.' }
    else { Fail "$name process is not running after the preview startup wait." }
}

Section 'Runtime logs'
$desktop = [Environment]::GetFolderPath('Desktop')
$logRoot = Join-Path $desktop 'TuringDesk-Logs'
$recentLogs = @()
if (Test-Path $logRoot -PathType Container) {
    $recentLogs = @(Get-ChildItem $logRoot -Filter '*.log' -File -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 12)
    foreach ($log in $recentLogs) {
        Write-Host ("  {0}  {1}" -f $log.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss'), $log.Name)
    }
}
else {
    Warn "Runtime log directory has not been created yet: $logRoot"
}

$reportRoot = if (Test-Path $logRoot -PathType Container) { $logRoot } else { $env:TEMP }
$reportPath = Join-Path $reportRoot ("arm64-real-machine-{0}.json" -f (Get-Date -Format 'yyyyMMdd-HHmmss'))
$report = [ordered]@{
    timestamp = (Get-Date).ToString('o')
    previewRoot = $PreviewRoot
    previewBuildSha = $buildSha
    checkoutSha = $checkoutSha
    windowsVersion = [Environment]::OSVersion.VersionString
    processor = if ($processor) { $processor.Name } else { $null }
    processorArchitecture = $architecture
    binaries = $binaryResults
    displays = $displayResults
    processes = $processResults
    recentLogs = @($recentLogs | ForEach-Object { $_.FullName })
}
$report | ConvertTo-Json -Depth 6 | Set-Content -Path $reportPath -Encoding UTF8
Pass "Machine report written to $reportPath"

Section 'Manual desktop acceptance checklist'
Write-Host '[ ] First-run Store Demo opens and the no-key golden path works.'
Write-Host '[ ] Aurora, Neon, and Ocean wallpapers are visibly different and animate smoothly.'
Write-Host '[ ] Wallpaper stays behind desktop icons and survives Show Desktop / Win+D.'
Write-Host '[ ] Widget stays above wallpaper and below normal application windows.'
Write-Host '[ ] Drag a widget from several points across its surface; movement follows the pointer.'
Write-Host '[ ] Restart MiaoMiao and confirm the widget position persists.'
Write-Host '[ ] Resize the Desktop Library continuously; no layout snap, clipping, or text overlap.'
Write-Host '[ ] Repeat UI checks at Windows scaling 100%, 125%, 150%, and 200% when available.'
Write-Host '[ ] Only one MiaoMiao tray icon is visible.'
Write-Host '[ ] Open MiaoMiao AI and verify API settings, saved-key mask, and vertical scrolling.'
Write-Host '[ ] Open the advanced workbench and verify Harness starts without destabilizing the desktop shell.'
Write-Host '[ ] If multiple displays are connected, move/test widgets on each display and verify topology recovery.'
Write-Host '[ ] Manually restart Explorer and verify wallpaper/widgets recover. Do not automate this on a primary workstation.'
Write-Host ''
Write-Host 'Physical ARM64 preflight complete. Visual/DesktopShell items above still require human verification.' -ForegroundColor Green
