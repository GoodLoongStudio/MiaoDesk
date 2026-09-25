param(
    [Parameter(Mandatory = $true)]
    [string]$ProductRoot
)

$ErrorActionPreference = 'Stop'
$wallpaperExe = Join-Path $ProductRoot 'MiaoDeskWallpaper.exe'
if (-not (Test-Path $wallpaperExe -PathType Leaf)) {
    throw "Widget visibility smoke test cannot find $wallpaperExe"
}
$contentGlassClock = Join-Path $ProductRoot 'Widgets\GlassClock.mdwidget\manifest.json'
$expectContentGlassClock = Test-Path $contentGlassClock -PathType Leaf

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
    [DllImport("user32.dll")]
    private static extern IntPtr GetParent(IntPtr window);
    [DllImport("user32.dll")]
    private static extern IntPtr GetWindow(IntPtr window, uint command);

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

    private static bool AboveDesktopIcons(IntPtr widget) {
        const uint GW_HWNDPREV = 3;
        for (var sibling = GetWindow(widget, GW_HWNDPREV); sibling != IntPtr.Zero;
             sibling = GetWindow(sibling, GW_HWNDPREV)) {
            var name = new StringBuilder(160);
            if (GetClassName(sibling, name, name.Capacity) > 0 &&
                String.Equals(name.ToString(), "SHELLDLL_DefView", StringComparison.OrdinalIgnoreCase)) return false;
        }
        return GetParent(widget) != IntPtr.Zero;
    }

    public static int PaintReadyWidgetCount(bool requireAboveIcons) {
        int found = 0;
        EnumWindows((top, ignored) => {
            if (IsPaintReadyWidget(top) && (!requireAboveIcons || AboveDesktopIcons(top))) found++;
            EnumChildWindows(top, (child, childIgnored) => {
                if (IsPaintReadyWidget(child) && (!requireAboveIcons || AboveDesktopIcons(child))) found++;
                return true;
            }, IntPtr.Zero);
            return true;
        }, IntPtr.Zero);
        return found;
    }

    // 壁纸停用时必须一个都看不到。类名与 IndependentWallpaperHost 的
    // kSurfaceClass 逐字一致;wallpaper 停用时根本不该创建这个表层。
    private static bool IsVisibleWallpaperSurface(IntPtr window) {
        var name = new StringBuilder(160);
        if (GetClassName(window, name, name.Capacity) <= 0 ||
            !String.Equals(name.ToString(), "MiaoDesk.Native.IndependentWallpaperSurface", StringComparison.OrdinalIgnoreCase) ||
            !IsWindowVisible(window)) return false;
        Rect rect;
        if (!GetClientRect(window, out rect) || rect.Right <= rect.Left || rect.Bottom <= rect.Top) return false;
        return GetParent(window) != IntPtr.Zero;
    }

    public static int VisibleWallpaperSurfaceCount() {
        int found = 0;
        EnumWindows((top, ignored) => {
            if (IsVisibleWallpaperSurface(top)) found++;
            EnumChildWindows(top, (child, childIgnored) => {
                if (IsVisibleWallpaperSurface(child)) found++;
                return true;
            }, IntPtr.Zero);
            return true;
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
$wallpaperIni = Join-Path $env:LOCALAPPDATA 'MiaoDesk\wallpaper.ini'

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
Enabled=1
"@
[IO.File]::WriteAllText($manifest, $manifestText, [Text.UnicodeEncoding]::new($false, $true))
New-Item -ItemType Directory -Force -Path (Split-Path $wallpaperIni -Parent) | Out-Null

function Set-WallpaperDisabledFixture {
    # Keep the fixture on the current schema and reassert it before every cold start.
    # This gate deliberately restarts the whole wallpaper/helper family multiple times;
    # a process from the previous phase may persist a fuller profile while shutting down.
    # The invariant under test is "cold start from Enabled=0 keeps wallpaper off while
    # Widgets recover", so every cold-start boundary must start from that exact state.
    [IO.File]::WriteAllText(
        $wallpaperIni,
        "[Wallpaper]`r`nVersion=9`r`nEnabled=0`r`n",
        [Text.UnicodeEncoding]::new($false, $true))
}

Set-WallpaperDisabledFixture

function Wait-WidgetCount([int]$Expected, [bool]$RequireAboveIcons, [int]$Seconds = 15) {
    $deadline = [DateTime]::UtcNow.AddSeconds($Seconds)
    do {
        Start-Sleep -Milliseconds 250
        $count = [MiaoDeskWidgetProbe]::PaintReadyWidgetCount($RequireAboveIcons)
        if ($count -eq $Expected) { return }
        if ($main.HasExited) { throw "MiaoDeskWallpaper exited during Widget lifecycle probe: $($main.ExitCode)" }
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Expected $Expected paint-ready Widget surface(s), observed $count."
}

function Stop-MiaoDeskWallpaperFamily {
    Get-Process MiaoDeskWallpaper -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    $deadline = [DateTime]::UtcNow.AddSeconds(5)
    do {
        Start-Sleep -Milliseconds 100
        $alive = @(Get-Process MiaoDeskWallpaper -ErrorAction SilentlyContinue).Count
        if ($alive -eq 0) { return }
    } while ([DateTime]::UtcNow -lt $deadline)
    throw 'MiaoDeskWallpaper process family did not stop before cold-start probe.'
}

$debugLog = Join-Path ([Environment]::GetFolderPath('Desktop')) 'MiaoDesk-Logs\desktop-debug.log'
Stop-MiaoDeskWallpaperFamily
Remove-Item $debugLog -Force -ErrorAction SilentlyContinue

try {
    $main = Start-Process -FilePath $wallpaperExe -WorkingDirectory $ProductRoot -PassThru
    Wait-WidgetCount 1 $true

    $disabled = $manifestText.Replace('Enabled=1', 'Enabled=0')
    [IO.File]::WriteAllText($manifest, $disabled, [Text.UnicodeEncoding]::new($false, $true))
    Wait-WidgetCount 0 $false

    [IO.File]::WriteAllText($manifest, $manifestText, [Text.UnicodeEncoding]::new($false, $true))
    Wait-WidgetCount 1 $true

    $second = $manifestText.Replace('Ids=ci-visible-widget', 'Ids=ci-visible-widget;ci-created-widget') + @"

[Widget.ci-created-widget]
Kind=native
Title=CI Created Widget
Source=native:today-tasks
MonitorId=
X=0.35
Y=0.05
Width=0.25
Height=0.30
Enabled=1
"@
    [IO.File]::WriteAllText($manifest, $second, [Text.UnicodeEncoding]::new($false, $true))
    Wait-WidgetCount 2 $true

    if ($expectContentGlassClock) {
        # This is the migration proof that the previous smoke test did not have:
        # shut down every helper, persist only a real Kind=content record, then
        # cold-start the product. The coordinator must decide to start WidgetHost,
        # WidgetHost must route content:<definitionId>, and the Surface must paint
        # above Explorer icons without any native:* record acting as a bootstrap.
        Stop-MiaoDeskWallpaperFamily
        $contentOnlyManifest = @"
[Widgets]
Ids=ci-content-widget

[Widget.ci-content-widget]
Kind=content
Title=CI Content GlassClock
Source=content:com.goodloong.glass-clock
MonitorId=
X=0.05
Y=0.05
Width=0.30
Height=0.30
Enabled=1
"@
        [IO.File]::WriteAllText($manifest, $contentOnlyManifest, [Text.UnicodeEncoding]::new($false, $true))
        Set-WallpaperDisabledFixture
        $main = Start-Process -FilePath $wallpaperExe -WorkingDirectory $ProductRoot -PassThru
        Wait-WidgetCount 1 $true
        Write-Host 'Content GlassClock cold-start/coordinator/desktop Surface lifecycle verified.' -ForegroundColor Green
    }

    $logDeadline = [DateTime]::UtcNow.AddSeconds(5)
    do {
        Start-Sleep -Milliseconds 200
        $logText = if (Test-Path $debugLog -PathType Leaf) {
            [IO.File]::ReadAllText($debugLog, [Text.UTF8Encoding]::new($false))
        } else { '' }
    } while ([string]::IsNullOrWhiteSpace($logText) -and [DateTime]::UtcNow -lt $logDeadline)

    foreach ($marker in @(
        '[WidgetHost] 原生组件宿主启动',
        '组件首次绘制成功',
        'mode=direct-gdi',
        'target=direct-swapchain',
        'parentNoRedirection=true',
        'paintReady=true',
        'zOrderValid=true',
        'wallpaper.enabled=false'
    )) {
        if (-not $logText.Contains($marker)) {
            throw "Native Widget diagnostic log is missing marker: $marker"
        }
    }
    if ($expectContentGlassClock) {
        # The staged migration proof must come from the dedicated Content host,
        # not the legacy Native GlassClock dogfood marker. Quick-build above still
        # proves the backward-compatible native fallback when Widgets/ is absent.
        foreach ($marker in @(
            '[ContentWidgetHost] Content 组件宿主启动',
            '[ContentWidgetHost] Content 组件首次绘制成功',
            'source="content:com.goodloong.glass-clock"'
        )) {
            if (-not $logText.Contains($marker)) {
                throw "Packaged GlassClock did not prove the dedicated Content Framework route. Missing marker: $marker"
            }
        }
        Write-Host 'Packaged GlassClock dedicated Content Framework host route verified.' -ForegroundColor Green
    } else {
        Write-Host 'Built-in Widgets directory is absent; native GlassClock fallback verified for quick-build layout.' -ForegroundColor Yellow
    }

    # P1-1 停用幂等:Shell reload 之后壁纸不得被重新拉起。
    #
    # 这里用"停掉整族再冷启"当 reload 的 CI 等价物:壁纸运行时进程消失、再被拉起来,
    # 而 wallpaper.ini 全程是 Enabled=0(本脚本开头写死的)。要证的是两件事同时成立 ——
    # 组件重建出来,壁纸没有跟着回来。只证后一件会把"什么都没起来"也算通过,只证前一件
    # 则盖不住"顺便把壁纸也拉起来了"这个回归,所以两个断言都要。
    $expectedAfterReload = if ($expectContentGlassClock) { 1 } else { 2 }
    Stop-MiaoDeskWallpaperFamily
    Set-WallpaperDisabledFixture
    $main = Start-Process -FilePath $wallpaperExe -WorkingDirectory $ProductRoot -PassThru
    Wait-WidgetCount $expectedAfterReload $true
    $resurrected = [MiaoDeskWidgetProbe]::VisibleWallpaperSurfaceCount()
    if ($resurrected -ne 0) {
        throw "Shell reload 后壁纸被重新拉起:wallpaper.ini 是 Enabled=0,却观察到 $resurrected 个可见的 IndependentWallpaperSurface。停用不是幂等的。"
    }
    Write-Host "停用幂等:运行时 reload 后壁纸保持停用(0 个 IndependentWallpaperSurface),组件重建 $expectedAfterReload 个。" -ForegroundColor Green
    Write-Host 'Native Widget create/disable/enable and icon-overlay lifecycle verified with wallpaper disabled.' -ForegroundColor Green
    Write-Host 'Widget direct-swapchain diagnostics and UTF-8 log markers verified.' -ForegroundColor Green

    $diagnostics = Join-Path $env:LOCALAPPDATA 'MiaoDesk\wallpaper.ini'
    if (Test-Path $diagnostics -PathType Leaf) {
        Write-Host 'Widget diagnostics:' -ForegroundColor Yellow
        Get-Content $diagnostics | Out-Host
    }
} catch {
    $details = ($_ | Out-String).Trim()
    $wallpaperIni = Join-Path $env:LOCALAPPDATA 'MiaoDesk\wallpaper.ini'
    if (Test-Path $wallpaperIni -PathType Leaf) {
        $details += "`n" + ((Get-Content $wallpaperIni | Out-String).Trim())
    }
    $debugLog = Join-Path ([Environment]::GetFolderPath('Desktop')) 'MiaoDesk-Logs\desktop-debug.log'
    if (Test-Path $debugLog -PathType Leaf) {
        $details += "`n[desktop-debug.log tail]`n" +
            ((Get-Content $debugLog -Encoding UTF8 | Select-Object -Last 60 | Out-String).Trim())
    }
    $details = $details.Replace('%','%25').Replace("`r",'%0D').Replace("`n",'%0A')
    Write-Host "::error title=Widget paint readiness failed::$details"
    throw
} finally {
    Get-Process MiaoDeskWallpaper -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    $env:LOCALAPPDATA = $previousLocalAppData
    Remove-Item $stateBase -Recurse -Force -ErrorAction SilentlyContinue
}