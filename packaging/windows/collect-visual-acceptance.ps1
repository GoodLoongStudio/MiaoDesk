param(
    [string]$OutputDirectory = "",
    [switch]$OpenResult
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ($env:OS -ne 'Windows_NT') {
    throw 'This visual-acceptance collector must run on Windows.'
}

if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $OutputDirectory = Join-Path ([Environment]::GetFolderPath('Desktop')) "MiaoDesk-Acceptance-$stamp"
}

New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

$nativeSource = @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

public static class MiaoDeskAcceptanceNative
{
    public delegate bool EnumWindowProc(IntPtr hwnd, IntPtr lParam);

    [StructLayout(LayoutKind.Sequential)]
    public struct RECT
    {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct POINT
    {
        public int X;
        public int Y;
    }

    [DllImport("user32.dll")]
    public static extern bool EnumWindows(EnumWindowProc callback, IntPtr lParam);

    [DllImport("user32.dll")]
    public static extern bool EnumChildWindows(IntPtr parent, EnumWindowProc callback, IntPtr lParam);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern int GetClassName(IntPtr hwnd, StringBuilder className, int maxCount);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern int GetWindowText(IntPtr hwnd, StringBuilder text, int maxCount);

    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr hwnd, out RECT rect);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern IntPtr GetProp(IntPtr hwnd, string name);

    [DllImport("user32.dll")]
    public static extern bool IsWindowVisible(IntPtr hwnd);

    [DllImport("user32.dll")]
    public static extern IntPtr GetParent(IntPtr hwnd);

    [DllImport("user32.dll")]
    public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint processId);

    [DllImport("user32.dll")]
    public static extern uint GetDpiForWindow(IntPtr hwnd);

    [DllImport("user32.dll")]
    public static extern IntPtr MonitorFromPoint(POINT point, uint flags);

    [DllImport("shcore.dll")]
    public static extern int GetDpiForMonitor(IntPtr monitor, int dpiType, out uint dpiX, out uint dpiY);

    public static uint[] MonitorDpi(int x, int y)
    {
        var point = new POINT { X = x, Y = y };
        var monitor = MonitorFromPoint(point, 2); // MONITOR_DEFAULTTONEAREST
        if (monitor == IntPtr.Zero) return new uint[] { 96, 96 };
        uint dpiX = 96, dpiY = 96;
        try
        {
            if (GetDpiForMonitor(monitor, 0, out dpiX, out dpiY) != 0)
                return new uint[] { 96, 96 };
        }
        catch (DllNotFoundException)
        {
            return new uint[] { 96, 96 };
        }
        catch (EntryPointNotFoundException)
        {
            return new uint[] { 96, 96 };
        }
        return new uint[] { dpiX, dpiY };
    }

    public static string ClassName(IntPtr hwnd)
    {
        var buffer = new StringBuilder(512);
        GetClassName(hwnd, buffer, buffer.Capacity);
        return buffer.ToString();
    }

    public static string WindowText(IntPtr hwnd)
    {
        var buffer = new StringBuilder(2048);
        GetWindowText(hwnd, buffer, buffer.Capacity);
        return buffer.ToString();
    }

    public static IntPtr[] AllWindows()
    {
        var values = new HashSet<IntPtr>();
        EnumWindowProc collect = (hwnd, _) =>
        {
            values.Add(hwnd);
            EnumChildWindows(hwnd, (child, __) => { values.Add(child); return true; }, IntPtr.Zero);
            return true;
        };
        EnumWindows(collect, IntPtr.Zero);
        var result = new IntPtr[values.Count];
        values.CopyTo(result);
        return result;
    }
}
'@

Add-Type -TypeDefinition $nativeSource -Language CSharp

function Get-WindowClass([IntPtr]$Hwnd) {
    return [MiaoDeskAcceptanceNative]::ClassName($Hwnd)
}

function Get-WindowTextSafe([IntPtr]$Hwnd) {
    return [MiaoDeskAcceptanceNative]::WindowText($Hwnd)
}

function Get-WindowRectObject([IntPtr]$Hwnd) {
    $rect = New-Object MiaoDeskAcceptanceNative+RECT
    if (-not [MiaoDeskAcceptanceNative]::GetWindowRect($Hwnd, [ref]$rect)) {
        return $null
    }
    return [ordered]@{
        left = $rect.Left
        top = $rect.Top
        right = $rect.Right
        bottom = $rect.Bottom
        width = [Math]::Max(0, $rect.Right - $rect.Left)
        height = [Math]::Max(0, $rect.Bottom - $rect.Top)
    }
}

function Test-WindowProperty([IntPtr]$Hwnd, [string]$Name) {
    return [MiaoDeskAcceptanceNative]::GetProp($Hwnd, $Name) -ne [IntPtr]::Zero
}

function Get-WindowPropertyInt([IntPtr]$Hwnd, [string]$Name) {
    $value = [MiaoDeskAcceptanceNative]::GetProp($Hwnd, $Name)
    if ($value -eq [IntPtr]::Zero) { return 0 }
    return $value.ToInt64()
}

function Get-MonitorSnapshot {
    $result = @()
    foreach ($screen in [System.Windows.Forms.Screen]::AllScreens) {
        $bounds = $screen.Bounds
        $centerX = $bounds.Left + [int]([Math]::Floor($bounds.Width / 2.0))
        $centerY = $bounds.Top + [int]([Math]::Floor($bounds.Height / 2.0))
        $dpi = [MiaoDeskAcceptanceNative]::MonitorDpi($centerX, $centerY)
        $dpiX = [uint32]$dpi[0]
        $dpiY = [uint32]$dpi[1]
        $result += [pscustomobject][ordered]@{
            deviceName = $screen.DeviceName
            primary = $screen.Primary
            left = $bounds.Left
            top = $bounds.Top
            right = $bounds.Right
            bottom = $bounds.Bottom
            width = $bounds.Width
            height = $bounds.Height
            orientation = if ($bounds.Height -gt $bounds.Width) { 'portrait' } else { 'landscape' }
            dpiX = $dpiX
            dpiY = $dpiY
            scalePercent = [Math]::Round(($dpiX / 96.0) * 100.0)
        }
    }
    return @($result | Sort-Object @{Expression='primary';Descending=$true}, left, top)
}

function Test-RectOverlapsMonitor($Rect, $Monitor) {
    if ($null -eq $Rect) { return $false }
    return ($Rect.left -lt $Monitor.right -and
            $Rect.right -gt $Monitor.left -and
            $Rect.top -lt $Monitor.bottom -and
            $Rect.bottom -gt $Monitor.top)
}

function Get-MiaoDeskSurfaceSnapshot {
    $result = @()
    foreach ($hwnd in [MiaoDeskAcceptanceNative]::AllWindows()) {
        $class = Get-WindowClass $hwnd
        if ($class -notin @(
            'MiaoDesk.Native.WidgetSurface',
            'MiaoDesk.Native.WebWallpaperHost'
        )) {
            continue
        }

        [uint32]$pid = 0
        [void][MiaoDeskAcceptanceNative]::GetWindowThreadProcessId($hwnd, [ref]$pid)
        $parent = [MiaoDeskAcceptanceNative]::GetParent($hwnd)
        $parentClass = if ($parent -ne [IntPtr]::Zero) { Get-WindowClass $parent } else { '' }
        $role = Get-WindowPropertyInt $hwnd 'MiaoDesk.WebSurface.Role'

        $result += [pscustomobject][ordered]@{
            hwnd = ('0x{0:X}' -f $hwnd.ToInt64())
            class = $class
            title = Get-WindowTextSafe $hwnd
            processId = $pid
            visible = [MiaoDeskAcceptanceNative]::IsWindowVisible($hwnd)
            dpi = [MiaoDeskAcceptanceNative]::GetDpiForWindow($hwnd)
            rect = Get-WindowRectObject $hwnd
            parentHwnd = if ($parent -eq [IntPtr]::Zero) { $null } else { '0x{0:X}' -f $parent.ToInt64() }
            parentClass = $parentClass
            role = $role
            environmentReady = Test-WindowProperty $hwnd 'MiaoDesk.WebSurface.EnvironmentReady'
            controllerReady = Test-WindowProperty $hwnd 'MiaoDesk.WebSurface.ControllerReady'
            navigationReady = Test-WindowProperty $hwnd 'MiaoDesk.WebSurface.NavigationReady'
            widgetPaintReady = Test-WindowProperty $hwnd 'MiaoDesk.Native.WidgetPaintReady'
        }
    }
    return @($result | Sort-Object class, title, hwnd)
}

function Get-MiaoDeskProcessSnapshot {
    $items = @()
    foreach ($process in @(Get-Process -ErrorAction SilentlyContinue |
                           Where-Object { $_.ProcessName -like 'MiaoDesk*' })) {
        $startTime = $null
        $processPath = $null
        try { $startTime = $process.StartTime.ToString('o') } catch {}
        try { $processPath = $process.Path } catch {}

        $items += [pscustomobject][ordered]@{
            name = $process.ProcessName
            id = $process.Id
            cpuSeconds = [Math]::Round([double]$process.CPU, 3)
            workingSetBytes = [int64]$process.WorkingSet64
            privateMemoryBytes = [int64]$process.PrivateMemorySize64
            handleCount = $process.HandleCount
            startTime = $startTime
            path = $processPath
        }
    }
    return @($items | Sort-Object name, id)
}

function Save-VirtualDesktopScreenshot([string]$Path) {
    $bounds = [System.Windows.Forms.SystemInformation]::VirtualScreen
    if ($bounds.Width -le 0 -or $bounds.Height -le 0) {
        throw 'Windows reported an empty virtual desktop.'
    }

    $bitmap = New-Object System.Drawing.Bitmap $bounds.Width, $bounds.Height
    try {
        $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
        try {
            $graphics.CopyFromScreen(
                $bounds.Left,
                $bounds.Top,
                0,
                0,
                $bounds.Size,
                [System.Drawing.CopyPixelOperation]::SourceCopy)
        }
        finally {
            $graphics.Dispose()
        }
        $bitmap.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
    }
    finally {
        $bitmap.Dispose()
    }

    return [ordered]@{
        left = $bounds.Left
        top = $bounds.Top
        width = $bounds.Width
        height = $bounds.Height
    }
}

$monitors = @(Get-MonitorSnapshot)
$surfaceBefore = @(Get-MiaoDeskSurfaceSnapshot)
$processes = @(Get-MiaoDeskProcessSnapshot)

$screenshotPath = Join-Path $OutputDirectory 'desktop.png'
$virtualScreen = Save-VirtualDesktopScreenshot $screenshotPath

$widgetSurfaces = @($surfaceBefore | Where-Object { $_.class -eq 'MiaoDesk.Native.WidgetSurface' })
$wallpaperSurfaces = @($surfaceBefore | Where-Object {
    $_.class -eq 'MiaoDesk.Native.WebWallpaperHost' -and $_.role -eq 1
})

$surfaceWarnings = @()
foreach ($surface in $surfaceBefore) {
    $onMonitor = $false
    foreach ($monitor in $monitors) {
        if (Test-RectOverlapsMonitor $surface.rect $monitor) {
            $onMonitor = $true
            break
        }
    }
    if (-not $onMonitor) {
        $surfaceWarnings += "Surface is outside every monitor: $($surface.class) $($surface.title) rect=$($surface.rect)"
    }
    if (-not $surface.visible) {
        $surfaceWarnings += "Surface not visible: $($surface.class) $($surface.title)"
    }
    if ($surface.role -eq 2 -and -not $surface.widgetPaintReady) {
        $surfaceWarnings += "Widget PaintReady missing: $($surface.title)"
    }
    if ($surface.class -eq 'MiaoDesk.Native.WebWallpaperHost') {
        if (-not $surface.environmentReady -or
            -not $surface.controllerReady -or
            -not $surface.navigationReady) {
            $surfaceWarnings += "Web surface lifecycle not ready: $($surface.title)"
        }
    }
}

$report = [pscustomobject][ordered]@{
    schema = 1
    generatedAt = (Get-Date).ToString('o')
    machine = $env:COMPUTERNAME
    user = $env:USERNAME
    os = [Environment]::OSVersion.VersionString
    virtualScreen = $virtualScreen
    monitors = $monitors
    screenshot = [IO.Path]::GetFileName($screenshotPath)
    summary = [ordered]@{
        monitorCount = $monitors.Count
        mixedDpi = (@($monitors | Select-Object -ExpandProperty dpiX -Unique).Count -gt 1)
        portraitMonitorCount = @($monitors | Where-Object { $_.orientation -eq 'portrait' }).Count
        surfaceCount = $surfaceBefore.Count
        widgetSurfaceCount = $widgetSurfaces.Count
        wallpaperSurfaceCount = $wallpaperSurfaces.Count
        processCount = $processes.Count
        warningCount = $surfaceWarnings.Count
    }
    surfaces = $surfaceBefore
    processes = $processes
    warnings = $surfaceWarnings
    manualChecks = @(
        'Wallpaper composition matches the expected MiaoCloud / NeonCity / MysticMoon visual baseline.',
        'Widgets are above the wallpaper and do not disappear behind desktop icons.',
        'No widget text, border or content is clipped on landscape or portrait monitors.',
        'Drag a widget across monitors with different DPI and confirm its size/text remains readable.',
        'Disable wallpaper and confirm widgets remain visible and interactive.',
        'Restart Explorer, wait for desktop recovery, rerun this collector and compare surfaces/parentClass/rect.',
        'Sleep/resume Windows and rerun this collector to check topology and surface recovery.'
    )
}

$reportPath = Join-Path $OutputDirectory 'acceptance.json'
$report | ConvertTo-Json -Depth 8 | Set-Content -Path $reportPath -Encoding UTF8

$summaryPath = Join-Path $OutputDirectory 'README.txt'
@(
    'MiaoDesk Windows visual acceptance evidence'
    "Generated: $($report.generatedAt)"
    "Screenshot: $screenshotPath"
    "Report: $reportPath"
    ''
    "Monitors: $($report.summary.monitorCount) · mixed DPI=$($report.summary.mixedDpi) · portrait=$($report.summary.portraitMonitorCount)"
    $($monitors | ForEach-Object { "  $($_.deviceName): $($_.width)x$($_.height) @ ($($_.left),$($_.top)) · $($_.dpiX) DPI ($($_.scalePercent)%) · $($_.orientation) · primary=$($_.primary)" })
    "Surfaces: $($report.summary.surfaceCount)"
    "Widgets: $($report.summary.widgetSurfaceCount)"
    "Wallpaper surfaces: $($report.summary.wallpaperSurfaceCount)"
    "MiaoDesk processes: $($report.summary.processCount)"
    "Warnings: $($report.summary.warningCount)"
    ''
    'Warnings:'
    $(if ($surfaceWarnings.Count) { $surfaceWarnings | ForEach-Object { "  - $_" } } else { '  (none)' })
    ''
    'Manual checks still required:'
    $($report.manualChecks | ForEach-Object { "  - $_" })
) | Set-Content -Path $summaryPath -Encoding UTF8

Write-Host "MiaoDesk visual acceptance evidence written to:" -ForegroundColor Green
Write-Host "  $OutputDirectory"
Write-Host "  Screenshot: $screenshotPath"
Write-Host "  JSON:       $reportPath"
Write-Host "  Summary:    $summaryPath"
if ($surfaceWarnings.Count -gt 0) {
    Write-Warning "$($surfaceWarnings.Count) runtime warning(s) were recorded."
}

if ($OpenResult) {
    Start-Process explorer.exe -ArgumentList @($OutputDirectory)
}
