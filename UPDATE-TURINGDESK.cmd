@echo off
setlocal EnableExtensions
chcp 65001 >nul

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
echo The updater does not perform automatic rollback.
pause
exit /b %RC%

:failed
echo.
echo Failed to download or start the updater.
echo The existing TuringDesk installation was not changed.
pause
exit /b 1
