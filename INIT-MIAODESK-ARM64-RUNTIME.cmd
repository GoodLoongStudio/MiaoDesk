@echo off
setlocal EnableExtensions
cd /d "%~dp0"

title MiaoDesk ARM64 Runtime Initializer

echo.
echo ========================================
echo   MiaoDesk ARM64 Runtime - One Time Init
echo ========================================
echo.
echo This downloads ONE full ARM64 preview, then persists Runtime/Pi/Goz/assets locally.
echo Future PREVIEW-MIAODESK-ARM64.cmd runs stay fast and reuse that local RuntimeCache.
echo.

where git >nul 2>nul
if errorlevel 1 (
  echo [ERROR] git was not found in PATH.
  goto :fail
)

echo [1/2] Updating initializer from main...
git pull --ff-only origin main
if errorlevel 1 goto :fail

echo.
echo [2/2] Downloading and caching the full ARM64 runtime...
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\initialize-arm64-runtime-cache.ps1"
set "RC=%ERRORLEVEL%"
if not "%RC%"=="0" goto :failcode

echo.
echo Runtime initialization complete.
echo You can use PREVIEW-MIAODESK-ARM64.cmd for fast acceptance from now on.
pause
exit /b 0

:failcode
echo.
echo Runtime initialization failed with exit code %RC%.
pause
exit /b %RC%

:fail
echo.
echo Runtime initialization bootstrap failed.
pause
exit /b 1
