@echo off
setlocal
cd /d "%~dp0"
call "%~dp0scripts\quick-test-arm64.cmd"
exit /b %ERRORLEVEL%
