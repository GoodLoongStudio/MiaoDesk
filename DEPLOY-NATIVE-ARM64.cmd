@echo off
setlocal EnableExtensions
call "%~dp0tools\dev\windows\DEPLOY-NATIVE-ARM64.cmd" %*
exit /b %ERRORLEVEL%
