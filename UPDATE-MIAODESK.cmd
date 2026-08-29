@echo off
setlocal EnableExtensions

title MiaoDesk ARM64 Updater
if not defined TD_UPDATER set "TD_UPDATER=%TEMP%\MiaoDesk-update-arm64.ps1"
if not defined TD_UPDATE_URL set "TD_UPDATE_URL=https://raw.githubusercontent.com/GoodLoongStudio/MiaoDesk/main/scripts/update-miaodesk-arm64.ps1"

echo.
echo ========================================
echo   MiaoDesk ARM64 One-Click Updater
echo ========================================
echo.
echo Downloading latest updater logic...

powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference='Stop'; Invoke-WebRequest -UseBasicParsing -Headers @{'Cache-Control'='no-cache'} $env:TD_UPDATE_URL -OutFile $env:TD_UPDATER"
if errorlevel 1 goto :failed

if "%TD_BOOTSTRAP_SELF_TEST%"=="1" goto :bootstrap_self_test

echo Running validated updater...
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%TD_UPDATER%"
set "RC=%ERRORLEVEL%"
del /q "%TD_UPDATER%" >nul 2>nul

if not "%RC%"=="0" goto :failed_code

echo.
echo Update finished successfully.
pause
exit /b 0

:bootstrap_self_test
echo Parsing downloaded updater with Windows PowerShell...
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference='Stop'; $t=[IO.File]::ReadAllText($env:TD_UPDATER,[Text.Encoding]::ASCII); $null=[scriptblock]::Create($t)"
set "RC=%ERRORLEVEL%"
del /q "%TD_UPDATER%" >nul 2>nul
if not "%RC%"=="0" exit /b %RC%
echo Updater bootstrap self-test passed.
exit /b 0

:failed_code
echo.
echo Update failed with exit code %RC%.
echo Automatic rollback is attempted if the install swap has started.
pause
exit /b %RC%

:failed
del /q "%TD_UPDATER%" >nul 2>nul
echo.
echo Failed to download or start the updater.
echo The existing MiaoDesk installation was not changed.
pause
exit /b 1
