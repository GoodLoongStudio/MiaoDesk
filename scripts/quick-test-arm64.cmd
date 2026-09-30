@echo off
setlocal
title MiaoDesk ARM64 Quick Test

cd /d "%~dp0.."

echo.
echo ========================================
echo   MiaoDesk ARM64 Quick Test
echo ========================================
echo.

where powershell.exe >nul 2>nul
if errorlevel 1 (
  echo [FAIL] powershell.exe not found.
  echo.
  pause
  exit /b 1
)

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
