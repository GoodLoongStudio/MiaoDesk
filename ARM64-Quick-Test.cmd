@echo off
setlocal
title MiaoDesk ARM64 Quick Test
cd /d "%~dp0"

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

echo [BOOT] Updating repository...
git pull --ff-only
if errorlevel 1 (
  echo.
  echo [FAIL] git pull failed.
  echo.
  pause
  exit /b 1
)

call "%~dp0scripts\quick-test-arm64.cmd"
exit /b %ERRORLEVEL%
