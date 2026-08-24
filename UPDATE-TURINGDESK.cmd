@echo off
setlocal EnableExtensions

title TuringDesk ARM64 Updater
set "TD_UPDATER=%TEMP%\TuringDesk-update-arm64.ps1"
set "TD_UPDATE_URL=https://raw.githubusercontent.com/GoodLoongStudio/TuringDesk/main/scripts/update-turingdesk-arm64.ps1"

echo.
echo ========================================
echo   TuringDesk ARM64 One-Click Updater
echo ========================================
echo.
echo Downloading latest updater logic...

powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference='Stop'; Invoke-WebRequest -UseBasicParsing -Headers @{'Cache-Control'='no-cache'} $env:TD_UPDATE_URL -OutFile $env:TD_UPDATER"
if errorlevel 1 goto :failed

echo Running validated updater...
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%TD_UPDATER%"
set "RC=%ERRORLEVEL%"
del /q "%TD_UPDATER%" >nul 2>nul

if not "%RC%"=="0" goto :failed_code

echo.
echo Update finished successfully.
pause
exit /b 0

:failed_code
echo.
echo Update failed with exit code %RC%.
echo The updater reports whether the installed package was touched.
pause
exit /b %RC%

:failed
del /q "%TD_UPDATER%" >nul 2>nul
echo.
echo Failed to download or start the updater.
echo The existing TuringDesk installation was not changed.
pause
exit /b 1
