param(
    [Parameter(Mandatory = $true)]
    [string]$ProductRoot
)

$ErrorActionPreference = 'Stop'
$exe = Join-Path $ProductRoot 'MiaoDesk.exe'
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
    throw "Creator window smoke cannot find MiaoDesk.exe at '$exe'."
}

Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
using System.Collections.Generic;

public sealed class MiaoDeskCreatorWindowInfo {
    public IntPtr Handle;
    public string ClassName;
    public string Title;
    public bool Visible;
    public IntPtr Owner;
}

public static class MiaoDeskCreatorWindowProbe {
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    static extern bool EnumWindows(EnumWindowsProc callback, IntPtr lParam);

    [DllImport("user32.dll")]
    static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint processId);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    static extern bool IsWindowVisible(IntPtr hWnd);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    static extern int GetClassName(IntPtr hWnd, StringBuilder className, int maxCount);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    static extern int GetWindowText(IntPtr hWnd, StringBuilder text, int maxCount);

    [DllImport("user32.dll")]
    static extern IntPtr GetWindow(IntPtr hWnd, uint command);

    const uint GW_OWNER = 4;

    public static MiaoDeskCreatorWindowInfo[] Find(uint processId) {
        var result = new List<MiaoDeskCreatorWindowInfo>();
        EnumWindows((hWnd, lParam) => {
            uint ownerPid;
            GetWindowThreadProcessId(hWnd, out ownerPid);
            if (ownerPid != processId) return true;

            var className = new StringBuilder(256);
            GetClassName(hWnd, className, className.Capacity);
            if (!String.Equals(className.ToString(), "MiaoDesk.Native.ContentCreatorDialog",
                               StringComparison.Ordinal)) return true;

            var title = new StringBuilder(512);
            GetWindowText(hWnd, title, title.Capacity);
            result.Add(new MiaoDeskCreatorWindowInfo {
                Handle = hWnd,
                ClassName = className.ToString(),
                Title = title.ToString(),
                Visible = IsWindowVisible(hWnd),
                Owner = GetWindow(hWnd, GW_OWNER)
            });
            return true;
        }, IntPtr.Zero);
        return result.ToArray();
    }
}
'@

function Wait-CreatorWindows {
    param(
        [Parameter(Mandatory = $true)][uint32]$ProcessId,
        [Parameter(Mandatory = $true)][int]$ExpectedCount,
        [int]$TimeoutSeconds = 12
    )
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        $windows = @([MiaoDeskCreatorWindowProbe]::Find($ProcessId))
        if ($windows.Count -ge $ExpectedCount) { return $windows }
        Start-Sleep -Milliseconds 200
    } while ([DateTime]::UtcNow -lt $deadline)
    return @([MiaoDeskCreatorWindowProbe]::Find($ProcessId))
}

function Invoke-CreatorRequest {
    param([Parameter(Mandatory = $true)][string]$Kind)
    $sender = Start-Process -FilePath $exe -ArgumentList "--content-creator=$Kind" -WorkingDirectory $ProductRoot -PassThru -Wait
    if ($sender.ExitCode -ne 0) {
        throw "Creator hand-off process for '$Kind' failed: exit=$($sender.ExitCode)"
    }
}

$previousLocalAppData = $env:LOCALAPPDATA
$stateBase = Join-Path $env:RUNNER_TEMP 'MiaoDesk-creator-window-smoke'
$env:LOCALAPPDATA = Join-Path $stateBase 'LocalAppData'

try {
    Get-Process MiaoDesk -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 400
    Remove-Item $stateBase -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path $env:LOCALAPPDATA | Out-Null

    $host = Start-Process -FilePath $exe -ArgumentList '--startup' -WorkingDirectory $ProductRoot -PassThru
    Start-Sleep -Milliseconds 800
    if ($host.HasExited) {
        throw "MiaoDesk creator host exited during startup: $($host.ExitCode)"
    }

    Invoke-CreatorRequest -Kind 'wallpaper'
    $windows = @(Wait-CreatorWindows -ProcessId ([uint32]$host.Id) -ExpectedCount 1)
    if ($windows.Count -ne 1) {
        throw "Expected exactly one creator after wallpaper request; found $($windows.Count)."
    }
    $wallpaper = $windows[0]
    if (-not $wallpaper.Visible) {
        throw 'Wallpaper creator exists but is not visible.'
    }
    if ($wallpaper.Owner -ne [IntPtr]::Zero) {
        throw "Wallpaper creator is still an owned window (owner=0x$($wallpaper.Owner.ToInt64().ToString('X')))."
    }
    if ($wallpaper.Title -notmatch 'AI 制作壁纸') {
        throw "Unexpected wallpaper creator title: '$($wallpaper.Title)'."
    }

    $firstHandle = $wallpaper.Handle
    Invoke-CreatorRequest -Kind 'wallpaper'
    $windows = @(Wait-CreatorWindows -ProcessId ([uint32]$host.Id) -ExpectedCount 1)
    if ($windows.Count -ne 1 -or $windows[0].Handle -ne $firstHandle) {
        throw 'Repeated wallpaper creator request did not reuse the existing window.'
    }

    Invoke-CreatorRequest -Kind 'widget'
    $windows = @(Wait-CreatorWindows -ProcessId ([uint32]$host.Id) -ExpectedCount 2)
    if ($windows.Count -ne 2) {
        throw "Expected wallpaper + widget creator windows; found $($windows.Count)."
    }
    $widget = @($windows | Where-Object { $_.Title -match 'AI 制作组件' })
    if ($widget.Count -ne 1 -or -not $widget[0].Visible) {
        throw 'Widget creator was not created as one visible window.'
    }
    if ($widget[0].Owner -ne [IntPtr]::Zero) {
        throw "Widget creator is still an owned window (owner=0x$($widget[0].Owner.ToInt64().ToString('X')))."
    }

    Write-Host 'AI creator cross-process window smoke passed: wallpaper visible/reused; widget visible; both independent top-level windows.' -ForegroundColor Cyan
}
catch {
    $details = ($_ | Out-String).Trim()
    $details = $details.Replace('%','%25').Replace("`r",'%0D').Replace("`n",'%0A')
    Write-Host "::error title=AI creator window smoke failed::$details"
    throw
}
finally {
    Get-Process MiaoDesk,MiaoDeskWallpaper,MiaoDeskHarness -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    $env:LOCALAPPDATA = $previousLocalAppData
    Remove-Item $stateBase -Recurse -Force -ErrorAction SilentlyContinue
}
