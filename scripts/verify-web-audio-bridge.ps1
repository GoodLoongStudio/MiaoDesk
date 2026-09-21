# The web audio bridge exists twice on purpose.
# src/desktop/wallpaper/web/WallpaperWebAudioBridge.js is the source of truth and
# what tests/WebAudioBridge.mjs runs; a byte-identical copy is embedded in
# WebDesktopSurfaceChild.cpp so the product ships without a runtime file read.
# Duplication is only safe if something enforces it, which is what this does.
#
# Written in PowerShell rather than Python to match every other guard in this
# repository and to avoid depending on a Python interpreter on the build agent.

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repo = Split-Path -Parent $PSScriptRoot
$jsPath = Join-Path $repo 'src\desktop\wallpaper\web\WallpaperWebAudioBridge.js'
$cppPath = Join-Path $repo 'src\desktop\wallpaper\web\WebDesktopSurfaceChild.cpp'

foreach ($path in @($jsPath, $cppPath)) {
    if (-not (Test-Path $path -PathType Leaf)) { throw "Web audio bridge guard input is missing: $path" }
}

$js = [System.IO.File]::ReadAllText($jsPath)
$cpp = [System.IO.File]::ReadAllText($cppPath)

$match = [regex]::Match($cpp, 'kWebAudioBridgeScript = R"MDWBRIDGE\(\r?\n(.*?)\r?\n\s*\)MDWBRIDGE";', 'Singleline')
if (-not $match.Success) {
    throw 'kWebAudioBridgeScript not found in WebDesktopSurfaceChild.cpp. The embedded copy may have been renamed or the raw-string delimiter changed.'
}

# [regex]::Match collapses \r\n to \n, so normalise the source the same way before
# comparing rather than reporting a difference that is only line endings.
$embedded = ($match.Groups[1].Value -replace "`r`n", "`n") + "`n"
$normalizedJs = ($js -replace "`r`n", "`n")
if ($embedded -cne $normalizedJs) {
    $diff = Compare-Object ($normalizedJs -split "`n") ($embedded -split "`n") |
        Select-Object -First 12 |
        ForEach-Object { "    $($_.SideIndicator) $($_.InputObject)" }
    throw "The embedded web audio bridge differs from its .js source of truth:`n$($diff -join "`n")"
}

# The shim is the only page -> host contract there is. If it ever grows a callable
# host surface, the one-directional posture documented in its header is gone and the
# security argument behind it has to be revisited before this ships.
$forbidden = @('chrome.webview.postMessage', 'external.postMessage', 'window.external')
foreach ($needle in $forbidden) {
    if ($normalizedJs.Contains($needle)) {
        throw "The web audio bridge must stay read-only but contains a page -> host call: $needle"
    }
}

Write-Host "Web audio bridge: embedded copy matches WallpaperWebAudioBridge.js ($($js.Length) chars) and stays read-only." -ForegroundColor Green
