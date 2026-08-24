@echo off
setlocal EnableExtensions

title TuringDesk ARM64 Updater
set "UPDATER=%TEMP%\TuringDesk-update-arm64.ps1"
set "URL=https://raw.githubusercontent.com/GoodLoongStudio/TuringDesk/main/scripts/update-turingdesk-arm64.ps1"

echo.
echo ========================================
echo   TuringDesk ARM64 One-Click Updater
echo ========================================
echo.
echo Downloading latest updater logic...

powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference='Stop'; Invoke-WebRequest -UseBasicParsing -Headers @{'Cache-Control'='no-cache'} '%URL%' -OutFile '%UPDATER%'"
if errorlevel 1 goto :failed

echo Validating updater encoding and syntax...
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference='Stop'; $p='%UPDATER%'; $u=New-Object System.Text.UTF8Encoding($false,$true); $t=[IO.File]::ReadAllText($p,$u); [scriptblock]::Create($t) ^| Out-Null; [IO.File]::WriteAllText($p,$t,[Text.Encoding]::Unicode)"
if errorlevel 1 goto :failed

powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%UPDATER%"
set "RC=%ERRORLEVEL%"
del /q "%UPDATER%" >nul 2>nul

if not "%RC%"=="0" goto :failed_code

echo.
echo Update finished successfully.
pause
exit /b 0

:failed_code
echo.
echo Update failed with exit code %RC%.
echo The updater does not perform automatic rollback after the install phase starts.
pause
exit /b %RC%

:failed
del /q "%UPDATER%" >nul 2>nul
echo.
echo Failed to download, decode, or parse the updater.
echo The existing TuringDesk installation was not changed.
pause
exit /b 1
