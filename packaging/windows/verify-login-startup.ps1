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
using System.Runtime.InteropServices;
public static class MiaoDeskStartupProbe {
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern IntPtr FindWindow(string className, string windowName);
    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool IsWindowVisible(IntPtr window);
}
'@

$previousLocalAppData = $env:LOCALAPPDATA
$stateBase = Join-Path $env:RUNNER_TEMP 'MiaoDesk-login-startup-smoke'
$env:LOCALAPPDATA = Join-Path $stateBase 'LocalAppData'
Remove-Item $stateBase -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $env:LOCALAPPDATA | Out-Null

try {
    $process = Start-Process -FilePath $executable -ArgumentList '--startup' `
        -WorkingDirectory $ProductRoot -PassThru
    $window = [IntPtr]::Zero
    for ($attempt = 0; $attempt -lt 40 -and $window -eq [IntPtr]::Zero; $attempt++) {
        Start-Sleep -Milliseconds 250
        if ($process.HasExited) { throw "MiaoDesk startup process exited early: $($process.ExitCode)" }
        $window = [MiaoDeskStartupProbe]::FindWindow('MiaoDesk.Native.SearchWindow', $null)
    }
    if ($window -eq [IntPtr]::Zero) {
        throw 'MiaoDesk startup process did not create its tray owner window within 10 seconds.'
    }
    if ([MiaoDeskStartupProbe]::IsWindowVisible($window)) {
        throw 'MiaoDesk displayed the search window during login startup.'
    }
    Write-Host 'Login startup created a live hidden tray owner without showing the search window.' -ForegroundColor Cyan
} catch {
    $details = ($_ | Out-String).Trim()
    $logPath = Join-Path ([Environment]::GetFolderPath('Desktop')) 'MiaoDesk-Logs\desktop-debug.log'
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
