@echo off
setlocal EnableExtensions
cd /d "%~dp0"

title TuringDesk ARM64 Development Preview

echo.
echo ========================================
echo   TuringDesk ARM64 Development Preview
echo ========================================
echo.
echo GitHub builds the preview; this PC only downloads and runs it.
echo No local CMake or Visual Studio build tools are required.
echo.

where git >nul 2>nul
if errorlevel 1 (
  echo [ERROR] git was not found in PATH.
  goto :fail
)

echo [1/2] Updating preview scripts from main...
git pull --ff-only origin main
if errorlevel 1 goto :fail

echo.
echo [2/2] Resolving and launching exact-head ARM64 preview...
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\download-arm64-preview.ps1"
set "RC=%ERRORLEVEL%"
if not "%RC%"=="0" goto :failcode
exit /b 0

:failcode
echo.
echo Preview failed with exit code %RC%.
pause
exit /b %RC%

:fail
echo.
echo Preview bootstrap failed.
pause
exit /b 1
