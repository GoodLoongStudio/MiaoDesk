param(
    [Parameter(Mandatory = $true)]
    [string]$ProductRoot
)

$ErrorActionPreference = 'Stop'
$executable = Join-Path $ProductRoot 'MiaoDesk.exe'
if (-not (Test-Path $executable -PathType Leaf)) {
    throw "Login startup smoke test cannot find $executable"
}

Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
using System.Collections.Generic;

public static class MiaoDeskStartupProbe {
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, IntPtr lParam);

    [DllImport("user32.dll")]
    public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint processId);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool IsWindowVisible(IntPtr hWnd);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern int GetClassName(IntPtr hWnd, StringBuilder className, int maxCount);

    public static string[] VisibleWindowClasses(uint processId) {
        var result = new List<string>();
        EnumWindows((hWnd, lParam) => {
            uint owner;
            GetWindowThreadProcessId(hWnd, out owner);
            if (owner == processId && IsWindowVisible(hWnd)) {
                var name = new StringBuilder(256);
                GetClassName(hWnd, name, name.Capacity);
                result.Add(name.ToString());
            }
            return true;
        }, IntPtr.Zero);
        return result.ToArray();
    }
}
'@

$previousLocalAppData = $env:LOCALAPPDATA
$stateBase = Join-Path $env:RUNNER_TEMP 'MiaoDesk-login-startup-smoke'
$env:LOCALAPPDATA = Join-Path $stateBase 'LocalAppData'
Remove-Item $stateBase -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $env:LOCALAPPDATA | Out-Null

# RuntimeLogger currently writes the user-visible diagnostic log under Desktop.
# Hosted Windows runners can make FindWindow unreliable for hidden WS_EX_TOOLWINDOW
# windows, so readiness is proved by the product's own post-Create startup marker.
$logPath = Join-Path ([Environment]::GetFolderPath('Desktop')) 'MiaoDesk-Logs\desktop-debug.log'
$startupMarker = 'MiaoDesk 登录启动成功，已静默驻留托盘'

try {
    $beforeLength = if (Test-Path $logPath -PathType Leaf) { (Get-Item $logPath).Length } else { 0L }
    $process = Start-Process -FilePath $executable -ArgumentList '--startup' `
        -WorkingDirectory $ProductRoot -PassThru

    $ready = $false
    for ($attempt = 0; $attempt -lt 60 -and -not $ready; $attempt++) {
        Start-Sleep -Milliseconds 250
        if ($process.HasExited) { throw "MiaoDesk startup process exited early: $($process.ExitCode)" }
        if (-not (Test-Path $logPath -PathType Leaf)) { continue }

        # Read only after startup began when possible, to avoid accepting a marker
        # left by a previous smoke run on the same hosted runner.
        $stream = [System.IO.File]::Open($logPath, 'Open', 'Read', 'ReadWrite')
        try {
            if ($beforeLength -gt 0 -and $beforeLength -lt $stream.Length) {
                $stream.Position = $beforeLength
            }
            $reader = [System.IO.StreamReader]::new($stream, [System.Text.Encoding]::UTF8, $true, 4096, $true)
            try { $newText = $reader.ReadToEnd() } finally { $reader.Dispose() }
        } finally {
            $stream.Dispose()
        }
        $ready = $newText.Contains($startupMarker)
    }
    if (-not $ready) {
        throw 'MiaoDesk startup process did not reach its silent tray-ready state within 15 seconds.'
    }

    # The login path must never expose a top-level UI. Checking by PID is more
    # reliable than FindWindow(className) for hidden layered/tool windows on CI.
    $visibleClasses = [MiaoDeskStartupProbe]::VisibleWindowClasses([uint32]$process.Id)
    if ($visibleClasses.Count -gt 0) {
        throw "MiaoDesk displayed top-level window(s) during login startup: $($visibleClasses -join ', ')"
    }

    Write-Host 'Login startup reached silent tray-ready state without showing a top-level window.' -ForegroundColor Cyan
} catch {
    $details = ($_ | Out-String).Trim()
    if (Test-Path $logPath -PathType Leaf) {
        $details += "`n[desktop-debug.log tail]`n" +
            ((Get-Content $logPath -Encoding UTF8 | Select-Object -Last 50 | Out-String).Trim())
    }
    $details = $details.Replace('%','%25').Replace("`r",'%0D').Replace("`n",'%0A')
    Write-Host "::error title=Login startup smoke failed::$details"
    throw
} finally {
    Get-Process MiaoDesk,MiaoDeskWallpaper,MiaoDeskHarness -ErrorAction SilentlyContinue |
        Stop-Process -Force -ErrorAction SilentlyContinue
    $env:LOCALAPPDATA = $previousLocalAppData
    Remove-Item $stateBase -Recurse -Force -ErrorAction SilentlyContinue
}
