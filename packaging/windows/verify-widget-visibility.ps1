param(
    [Parameter(Mandatory = $true)]
    [string]$ProductRoot
)

$ErrorActionPreference = 'Stop'
$wallpaperExe = Join-Path $ProductRoot 'MiaoDeskWallpaper.exe'
if (-not (Test-Path $wallpaperExe -PathType Leaf)) {
    throw "Widget visibility smoke test cannot find $wallpaperExe"
}

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;

public static class MiaoDeskWidgetProbe {
    private delegate bool EnumWindowsProc(IntPtr window, IntPtr parameter);

    [DllImport("user32.dll")]
    private static extern bool EnumWindows(EnumWindowsProc callback, IntPtr parameter);
    [DllImport("user32.dll")]
    private static extern bool EnumChildWindows(IntPtr parent, EnumWindowsProc callback, IntPtr parameter);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern int GetClassName(IntPtr window, StringBuilder className, int capacity);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern IntPtr GetProp(IntPtr window, string name);
    [DllImport("user32.dll")]
    private static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")]
    private static extern bool GetClientRect(IntPtr window, out Rect rect);

    [StructLayout(LayoutKind.Sequential)]
    private struct Rect { public int Left, Top, Right, Bottom; }

    private static bool IsPaintReadyWidget(IntPtr window) {
        var name = new StringBuilder(160);
        if (GetClassName(window, name, name.Capacity) <= 0 ||
            !String.Equals(name.ToString(), "MiaoDesk.Native.WidgetSurface", StringComparison.OrdinalIgnoreCase) ||
            !IsWindowVisible(window)) return false;
        Rect rect;
        if (!GetClientRect(window, out rect) || rect.Right <= rect.Left || rect.Bottom <= rect.Top) return false;
        return GetProp(window, "MiaoDesk.Native.WidgetPaintReady") != IntPtr.Zero;
    }

    public static bool HasPaintReadyWidget() {
        bool found = false;
        EnumWindows((top, ignored) => {
            if (IsPaintReadyWidget(top)) { found = true; return false; }
            EnumChildWindows(top, (child, childIgnored) => {
                if (!IsPaintReadyWidget(child)) return true;
                found = true;
                return false;
            }, IntPtr.Zero);
            return !found;
        }, IntPtr.Zero);
        return found;
    }
}
'@

$previousLocalAppData = $env:LOCALAPPDATA
$stateBase = Join-Path $env:RUNNER_TEMP 'MiaoDesk-widget-visibility-smoke'
$env:LOCALAPPDATA = Join-Path $stateBase 'LocalAppData'
$widgetRoot = Join-Path $env:LOCALAPPDATA 'MiaoDesk\DesktopWidgets'
$manifest = Join-Path $widgetRoot 'widgets.ini'

Remove-Item $stateBase -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path (Join-Path $widgetRoot 'Packages') | Out-Null
$manifestText = @"
[Widgets]
Ids=ci-visible-widget

[Widget.ci-visible-widget]
Kind=native
Title=CI Visible Widget
Source=native:glass-clock
MonitorId=
X=0.05
Y=0.05
Width=0.25
Height=0.20
ZIndex=100
Enabled=1
ManagedSource=0
"@
[IO.File]::WriteAllText($manifest, $manifestText, [Text.UnicodeEncoding]::new($false, $true))

try {
    $main = Start-Process -FilePath $wallpaperExe -WorkingDirectory $ProductRoot -PassThru
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    do {
        Start-Sleep -Milliseconds 500
        if ([MiaoDeskWidgetProbe]::HasPaintReadyWidget()) {
            Write-Host 'Paint-ready native Widget surface verified.' -ForegroundColor Green
            return
        }
        if ($main.HasExited) { throw "MiaoDeskWallpaper exited before Widget became paint-ready: $($main.ExitCode)" }
    } while ([DateTime]::UtcNow -lt $deadline)

    $diagnostics = Join-Path $env:LOCALAPPDATA 'MiaoDesk\wallpaper.ini'
    if (Test-Path $diagnostics -PathType Leaf) {
        Write-Host 'Widget diagnostics:' -ForegroundColor Yellow
        Get-Content $diagnostics | Out-Host
    }
    throw 'No paint-ready MiaoDesk.Native.WidgetSurface appeared within 15 seconds.'
} catch {
    $details = ($_ | Out-String).Trim()
    $wallpaperIni = Join-Path $env:LOCALAPPDATA 'MiaoDesk\wallpaper.ini'
    if (Test-Path $wallpaperIni -PathType Leaf) {
        $details += "`n" + ((Get-Content $wallpaperIni | Out-String).Trim())
    }
    $details = $details.Replace('%','%25').Replace("`r",'%0D').Replace("`n",'%0A')
    Write-Host "::error title=Native Widget paint readiness failed::$details"
    throw
} finally {
    Get-Process MiaoDeskWallpaper -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    $env:LOCALAPPDATA = $previousLocalAppData
    Remove-Item $stateBase -Recurse -Force -ErrorAction SilentlyContinue
}
