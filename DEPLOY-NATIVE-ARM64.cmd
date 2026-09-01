@echo off
setlocal EnableExtensions
cd /d "%~dp0"
git pull --ff-only
if errorlevel 1 exit /b %ERRORLEVEL%
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\deploy-native-arm64.ps1" %*
exit /b %ERRORLEVEL%
