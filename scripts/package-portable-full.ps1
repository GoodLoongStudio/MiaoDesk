param(
    [Parameter(Mandatory = $true)][string]$SourceDirectory,
    [Parameter(Mandatory = $true)][ValidateSet('arm64','x64')][string]$Architecture,
    [Parameter(Mandatory = $true)][string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$source = [IO.Path]::GetFullPath($SourceDirectory)
$out = [IO.Path]::GetFullPath($OutputDirectory)
if (-not (Test-Path $source -PathType Container)) {
    throw "Portable package source does not exist: $source"
}

Remove-Item $out -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $out | Out-Null

$archiveName = "MiaoDesk-$Architecture-full.tar.gz"
$archivePath = Join-Path $out $archiveName

# Keep the expanded node_modules tree inside tar.gz in the GitHub Artifact so
# Explorer never has to unpack thousands of deep dependency paths.  The payload
# itself is location-independent: the extractor may place it beside this script
# or in any directory supplied by the user.
& tar.exe -czf $archivePath -C $source .
if ($LASTEXITCODE -ne 0 -or -not (Test-Path $archivePath -PathType Leaf)) {
    throw "Failed to create portable $Architecture payload."
}

$extractCmd = Join-Path $out 'EXTRACT-MIAODESK.cmd'
$archiveCommand = 'set "ARCHIVE=%~dp0{0}"' -f $archiveName
Set-Content -Path $extractCmd -Encoding ASCII -Value @(
    '@echo off',
    'setlocal EnableExtensions',
    $archiveCommand,
    'if not exist "%ARCHIVE%" (',
    '  echo [MiaoDesk] Package payload is missing: %ARCHIVE%',
    '  pause',
    '  exit /b 2',
    ')',
    'if not "%~1"=="" (',
    '  set "DEST=%~1"',
    ') else (',
    '  set "DEST=%~dp0MiaoDesk"',
    ')',
    'if not exist "%DEST%" mkdir "%DEST%" >nul 2>&1',
    'if not exist "%DEST%" (',
    '  echo [MiaoDesk] Cannot create destination: %DEST%',
    '  pause',
    '  exit /b 3',
    ')',
    'echo [MiaoDesk] Extracting to %DEST% ...',
    'tar.exe -xzf "%ARCHIVE%" -C "%DEST%"',
    'if errorlevel 1 (',
    '  echo [MiaoDesk] Extraction failed at: %DEST%',
    '  echo [MiaoDesk] The package does not require C:\MD or any other fixed install directory.',
    '  pause',
    '  exit /b 4',
    ')',
    'if not exist "%DEST%\MiaoDesk.exe" (',
    '  echo [MiaoDesk] Extraction finished but MiaoDesk.exe is missing.',
    '  pause',
    '  exit /b 5',
    ')',
    'echo [MiaoDesk] Ready: %DEST%',
    'start "" "%DEST%\MiaoDesk.exe"',
    'exit /b 0'
)

Set-Content -Path (Join-Path $out 'README-FIRST.txt') -Encoding UTF8 -Value @(
    "MiaoDesk $Architecture FULL portable package",
    '',
    'MiaoDesk does not require a fixed install path.',
    '',
    'Recommended:',
    '  1. Extract this small GitHub Artifact ZIP anywhere.',
    '  2. Double-click EXTRACT-MIAODESK.cmd.',
    '  3. The full app is extracted to a MiaoDesk folder beside this script.',
    '  4. MiaoDesk.exe starts automatically.',
    '',
    'Custom destination:',
    '  EXTRACT-MIAODESK.cmd "D:\Apps\MiaoDesk"',
    '  EXTRACT-MIAODESK.cmd "%LOCALAPPDATA%\Programs\MiaoDesk"',
    '',
    'The archive contains the complete Node / DeepSeek Harness / Pi / goz runtime tree.',
    'Runtime discovery is relative to MiaoDesk.exe; moving the complete MiaoDesk folder keeps those relationships intact.',
    '',
    'IMPORTANT: do not open the .tar.gz and drag its node_modules tree out with Windows Explorer.',
    'Use EXTRACT-MIAODESK.cmd (Windows tar.exe) so Explorer never walks the deep dependency tree.'
)

# Prove the release is not coupled to C:\MD or another short/fixed root.  Verify
# the exact user-facing archive under a nested path containing spaces, which
# catches accidental current-directory and hard-coded-root dependencies.
$verifyRoot = Join-Path $env:TEMP ("MiaoDesk package verification\$Architecture\custom install root")
Remove-Item $verifyRoot -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $verifyRoot | Out-Null
try {
    & tar.exe -xzf $archivePath -C $verifyRoot
    if ($LASTEXITCODE -ne 0) { throw "Portable $Architecture archive re-extraction failed outside a fixed install root." }
    foreach ($relative in @(
        'MiaoDesk.exe',
        'MiaoDeskWallpaper.exe',
        'MiaoDeskHarness.exe',
        'Runtime\Node\node.exe',
        'Runtime\Node\node_modules\@deepseek-ai\dsh\lib\bin.js',
        'Pi\node_modules\@earendil-works\pi-coding-agent\dist\cli.js',
        'Goz\goz.exe',
        'Goz\gozd.exe'
    )) {
        if (-not (Test-Path (Join-Path $verifyRoot $relative) -PathType Leaf)) {
            throw "Portable $Architecture archive is missing: $relative"
        }
    }
} finally {
    Remove-Item $verifyRoot -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host "Portable $Architecture artifact is ready: $out" -ForegroundColor Green
Write-Host "Payload: $archivePath" -ForegroundColor DarkGray
