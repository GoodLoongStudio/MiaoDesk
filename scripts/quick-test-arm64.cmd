@echo off
setlocal
title MiaoDesk ARM64 Quick Test

cd /d "%~dp0.."

echo.
echo ========================================
echo   MiaoDesk ARM64 Quick Test
echo ========================================
echo.

where git.exe >nul 2>nul
if errorlevel 1 (
  echo [FAIL] git.exe not found.
  echo.
  pause
  exit /b 1
)

echo [1/2] Updating quick-test scripts...
git pull --ff-only
if errorlevel 1 (
  echo.
  echo [FAIL] git pull failed.
  echo.
  pause
  exit /b 1
)

where powershell.exe >nul 2>nul
if errorlevel 1 (
  echo [FAIL] powershell.exe not found.
  echo.
  pause
  exit /b 1
)

echo.
echo [2/2] Starting ARM64 quick test...
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0quick-test-arm64.ps1"
set "RC=%ERRORLEVEL%"

echo.
if not "%RC%"=="0" (
  echo ========================================
  echo   ARM64 QUICK TEST FAILED
  echo   Exit code: %RC%
  echo ========================================
) else (
  echo ========================================
  echo   ARM64 QUICK TEST READY
  echo ========================================
)

echo.
pause
exit /b %RC%
