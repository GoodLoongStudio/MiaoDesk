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

# IMPORTANT: do not expose the expanded node_modules tree directly in a GitHub Artifact ZIP.
# Windows Explorer still fails on deeply nested dependency paths on many machines. We keep the
# entire portable payload inside one tar.gz and let the bundled extractor use Windows tar.exe,
# which avoids Explorer walking each long member name.
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
    '  set "DEST=C:\MD"',
    '  if not exist "C:\MD" mkdir "C:\MD" >nul 2>&1',
    '  if not exist "C:\MD" set "DEST=%LOCALAPPDATA%\MD"',
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
    '  echo [MiaoDesk] Extraction failed. Please keep this package on a local drive and retry.',
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
    'IMPORTANT: do not open the .tar.gz and drag its node_modules tree out with Windows Explorer.',
    'That can trigger Error 0x80010135 / Path too long on otherwise valid files.',
    '',
    'Recommended:',
    '  1. Extract this small GitHub Artifact ZIP anywhere.',
    '  2. Double-click EXTRACT-MIAODESK.cmd.',
    '  3. It extracts the full runtime with Windows tar.exe to C:\MD (or %LOCALAPPDATA%\MD if C:\MD is unavailable).',
    '  4. MiaoDesk.exe starts automatically.',
    '',
    'Custom destination:',
    '  EXTRACT-MIAODESK.cmd D:\MD',
    '',
    'The archive contains the complete Node / DeepSeek Harness / Pi / goz runtime tree.'
)

# CI proves the exact user-facing archive can be extracted without Explorer. Use a deliberately
# short destination to keep third-party Node dependency paths well away from legacy MAX_PATH.
$verifyRoot = "C:\mdpkg-$Architecture"
Remove-Item $verifyRoot -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $verifyRoot | Out-Null
try {
    & tar.exe -xzf $archivePath -C $verifyRoot
    if ($LASTEXITCODE -ne 0) { throw "Portable $Architecture archive re-extraction failed." }
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
