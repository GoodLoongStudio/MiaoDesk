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

powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\download-arm64-preview.ps1"
set "RC=%ERRORLEVEL%"
if not "%RC%"=="0" (
  echo.
  echo Preview failed with exit code %RC%.
  pause
)
exit /b %RC%
