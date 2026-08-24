@echo off
setlocal EnableExtensions
cd /d "%~dp0"

title TuringDesk ARM64 One-Click Deploy

echo.
echo ========================================
echo   TuringDesk ARM64 One-Click Deploy
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

where gh >nul 2>nul
if errorlevel 1 (
  echo [ERROR] GitHub CLI ^(gh^) was not found in PATH.
  goto :fail
)

echo [1/2] Updating TuringDesk main...
git pull --ff-only
if errorlevel 1 goto :fail

echo.
echo [2/2] Validating and deploying the complete TuringDesk ARM64 package...
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\deploy-native-arm64.ps1"
if errorlevel 1 goto :fail

echo.
echo ========================================
echo   SUCCESS - TuringDesk ARM64 is running
echo ========================================
timeout /t 2 /nobreak >nul
exit /b 0

:fail
echo.
echo ========================================
echo   DEPLOY FAILED
echo   See the error above.
echo ========================================
echo.
pause
exit /b 1
