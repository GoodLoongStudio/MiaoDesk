@echo off
setlocal EnableExtensions
cd /d "%~dp0"

title MiaoDesk ARM64 Smart Developer Runner

echo.
echo ========================================
echo   MiaoDesk ARM64 Smart Developer Runner
echo ========================================
echo.

where git >nul 2>nul
if errorlevel 1 (
  echo [ERROR] git was not found in PATH.
  goto :fail
)

where powershell.exe >nul 2>nul
if errorlevel 1 (
  echo [ERROR] powershell.exe was not found.
  goto :fail
)

echo [1/2] Updating MiaoDesk main...
git pull --ff-only
if errorlevel 1 goto :fail

echo.
echo [2/2] Selecting the lightest development path...
if /I "%~1"=="full" (
  powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\deploy-native-arm64.ps1" -Mode full
) else if /I "%~1"=="preview" (
  powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\deploy-native-arm64.ps1" -Mode preview
) else (
  powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\deploy-native-arm64.ps1" -Mode auto
)
if errorlevel 1 goto :fail

echo.
echo ========================================
echo   SUCCESS - Development task completed
echo ========================================
timeout /t 2 /nobreak >nul
exit /b 0

:fail
echo.
echo ========================================
echo   DEVELOPMENT TASK FAILED
echo   See the error above.
echo ========================================
echo.
pause
exit /b 1
