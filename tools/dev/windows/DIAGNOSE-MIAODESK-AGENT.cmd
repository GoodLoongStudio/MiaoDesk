@echo off
setlocal EnableExtensions
for %%I in ("%~dp0..\..\..") do set "REPO_ROOT=%%~fI"
cd /d "%REPO_ROOT%"

title MiaoDesk Agent Diagnostics

echo.
echo ========================================
echo   MiaoDesk Agent Diagnostics
echo ========================================
echo.
echo This prints runtime/config/log diagnostics only.
echo API keys are never read or printed.
echo.

git pull --ff-only origin main
if errorlevel 1 goto :fail

powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%REPO_ROOT%\scripts\diagnose-miaodesk-agent.ps1"
set "RC=%ERRORLEVEL%"
echo.
if not "%RC%"=="0" (
  echo Diagnostics failed with exit code %RC%.
) else (
  echo Diagnostics complete.
)
pause
exit /b %RC%

:fail
echo.
echo Failed to update diagnostics from main.
pause
exit /b 1
