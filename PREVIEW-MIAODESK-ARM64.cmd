@echo off
setlocal EnableExtensions
cd /d "%~dp0"

title MiaoDesk ARM64 Fast UI Preview

echo.
echo ========================================
echo   MiaoDesk ARM64 FAST UI Preview
echo ========================================
echo.
echo GitHub builds the preview; this PC downloads UI executables, DLLs, and the small built-in wallpaper assets.
echo Large Node / Harness / Pi runtime payloads are NOT downloaded for quick acceptance.
echo Persistent RuntimeCache is reused first; an existing NativeTest runtime is the fallback.
echo If Agent runtime is missing, run INIT-MIAODESK-ARM64-RUNTIME.cmd once.
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
echo [2/2] Resolving and launching FAST exact-head ARM64 UI preview...
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\download-arm64-fast-preview.ps1"
set "RC=%ERRORLEVEL%"
if not "%RC%"=="0" goto :failcode
exit /b 0

:failcode
echo.
echo Fast preview failed with exit code %RC%.
pause
exit /b %RC%

:fail
echo.
echo Fast preview bootstrap failed.
pause
exit /b 1
