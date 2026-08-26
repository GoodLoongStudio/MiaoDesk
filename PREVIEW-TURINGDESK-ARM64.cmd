@echo off
setlocal EnableExtensions
cd /d "%~dp0"

title TuringDesk ARM64 Development Preview

echo.
echo ========================================
echo   TuringDesk ARM64 Development Preview
echo ========================================
echo.

echo Updating main and starting the lightweight local preview...
call "%~dp0DEPLOY-NATIVE-ARM64.cmd" preview
exit /b %ERRORLEVEL%
