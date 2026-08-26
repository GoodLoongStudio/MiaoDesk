param(
    [ValidateSet('baseline','settings','search','explorer','monitor')]
    [string]$Phase = 'baseline'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$runner = Join-Path $PSScriptRoot 'run-widget-runtime-acceptance.ps1'
$localAppData = if ($env:LOCALAPPDATA) { $env:LOCALAPPDATA } else { throw 'LOCALAPPDATA is unavailable.' }
$installedDir = Join-Path $localAppData 'TuringDesk\NativeTest'
$probe = Join-Path $installedDir 'TuringDeskWidgetAcceptance.exe'
$installedMarker = Join-Path $installedDir '.installed-build-sha'

function Get-PeMachine([string]$Path) {
    $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
    try {
        if ($stream.Length -lt 64) { throw "PE image is too small: $Path" }
        $reader = New-Object IO.BinaryReader($stream)
        if ($reader.ReadUInt16() -ne 0x5A4D) { throw "PE image is missing MZ signature: $Path" }
        $stream.Position = 0x3C
        $peOffset = $reader.ReadInt32()
        if ($peOffset -lt 0 -or ($peOffset + 6) -gt $stream.Length) { throw "PE header offset is invalid: $Path" }
        $stream.Position = $peOffset
        if ($reader.ReadUInt32() -ne 0x00004550) { throw "PE image is missing PE signature: $Path" }
        return $reader.ReadUInt16()
    }
    finally { $stream.Dispose() }
}

if (-not (Test-Path -LiteralPath $runner -PathType Leaf)) {
    throw "M3 acceptance phase runner is missing: $runner"
}
if (-not (Test-Path -LiteralPath $probe -PathType Leaf)) {
    throw "Installed M3 acceptance probe is missing: $probe. Run the ARM64 one-click updater for a build that packages TuringDeskWidgetAcceptance.exe."
}
if (-not (Test-Path -LiteralPath $installedMarker -PathType Leaf)) {
    throw "Installed validated build marker is missing: $installedMarker. Re-run the ARM64 one-click updater before M3 acceptance."
}

$buildSha = ([string](Get-Content -LiteralPath $installedMarker -Raw -ErrorAction Stop)).Trim().ToLowerInvariant()
if ($buildSha -notmatch '^[0-9a-f]{40}$') {
    throw "Installed validated build marker is malformed: $buildSha"
}

$machine = Get-PeMachine -Path $probe
if ($machine -ne 0xAA64) {
    throw ('Installed M3 acceptance probe is not ARM64. PE machine=0x{0:X4} expected=0xAA64 path={1}' -f $machine, $probe)
}

if (Get-Command git.exe -ErrorAction SilentlyContinue) {
    $checkoutSha = (& git.exe -C $root rev-parse HEAD 2>$null)
    if ($LASTEXITCODE -eq 0 -and -not [string]::IsNullOrWhiteSpace($checkoutSha)) {
        $checkoutSha = ([string]$checkoutSha).Trim().ToLowerInvariant()
        if ($checkoutSha -ne $buildSha) {
            throw "Installed M3 acceptance build does not match this checkout. installed=$buildSha checkout=$checkoutSha. Update TuringDesk or switch the checkout before collecting evidence."
        }
    }
}

Write-Host "Running installed ARM64 M3 Widget acceptance: phase=$Phase" -ForegroundColor Cyan
Write-Host "Probe: $probe" -ForegroundColor DarkGray
Write-Host "Installed validated build: $buildSha" -ForegroundColor DarkGray
Write-Host 'Probe architecture: ARM64 (PE machine 0xAA64)' -ForegroundColor DarkGray

& $runner -BuildDir $installedDir -Phase $Phase
exit $LASTEXITCODE
