; MiaoDesk NSIS installer
; Build example:
;   makensis /DSTAGE_DIR="C:\pkg\MiaoDesk\x64" /DOUTPUT_FILE="MiaoDesk-x64-Setup.exe" packaging\nsis\MiaoDesk.nsi
;
; STAGE_DIR must be the already-staged product root produced by:
;   cmake --install <short-build-dir> --config Release --prefix <stage-dir>
; followed by MiaoDesk's production RuntimeBundle materialization.
;
; This installer intentionally does NOT modify LongPathsEnabled, create subst
; drives, or require users to change Windows policy/registry settings.

Unicode true

!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "FileFunc.nsh"
!include "Sections.nsh"

!ifndef STAGE_DIR
    !error "STAGE_DIR is required and must point to the MiaoDesk install staging root."
!endif

!ifndef OUTPUT_FILE
    !define OUTPUT_FILE "MiaoDesk-x64-Setup.exe"
!endif

!define PRODUCT_NAME "MiaoDesk"
!define PRODUCT_VERSION "0.1.0"
!define PRODUCT_PUBLISHER "GoodLoongStudio"
!define PRODUCT_REG_KEY "Software\GoodLoongStudio\MiaoDesk"
!define UNINSTALL_REG_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\MiaoDesk"

Name "${PRODUCT_NAME}"
OutFile "${OUTPUT_FILE}"

; Prefer the conventional short all-users path. Standard users who cannot
; elevate automatically fall back to the shallow per-user path
; %LOCALAPPDATA%\MiaoDesk (not a deep Programs/vendor/product hierarchy).
InstallDir "$PROGRAMFILES64\MiaoDesk"
RequestExecutionLevel highest
SetRegView 64
SetCompressor /SOLID lzma
ShowInstDetails show
ShowUninstDetails show

Var InstallMode
Var InstallRoot
Var WindowsRoot
Var JunctionBase
Var JunctionPath

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_COMPONENTS
!define MUI_PAGE_CUSTOMFUNCTION_LEAVE CheckInstallDirectoryLength
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

!insertmacro MUI_LANGUAGE "SimpChinese"

Function .onInit
    ; RequestExecutionLevel=highest elevates an administrator account, but a
    ; genuinely limited account remains usable with a per-user install.
    UserInfo::GetAccountType
    Pop $0
    StrCmp $0 "Admin" 0 LimitedUser

    StrCpy $InstallMode "all"
    SetShellVarContext all
    StrCpy $INSTDIR "$PROGRAMFILES64\MiaoDesk"
    Return

LimitedUser:
    StrCpy $InstallMode "current"
    SetShellVarContext current
    StrCpy $INSTDIR "$LOCALAPPDATA\MiaoDesk"
FunctionEnd

Function CheckInstallDirectoryLength
    StrLen $0 "$INSTDIR"
    ; IntCmp: equal, less, greater. Paths over 85 chars are warned but are not
    ; blocked, per product requirement.
    IntCmp $0 85 PathLengthOK PathLengthOK PathLengthWarning

PathLengthWarning:
    MessageBox MB_OK|MB_ICONEXCLAMATION \
        "当前安装路径长度为 $0 个字符。$\r$\n$\r$\n部分旧版 Windows 组件仍可能受传统 260 字符路径限制。建议改用较短路径，例如：$\r$\nC:\Program Files\MiaoDesk$\r$\nD:\Apps\MiaoDesk$\r$\n$\r$\n安装不会被强制阻止。"

PathLengthOK:
FunctionEnd

Section "!MiaoDesk 核心程序" SEC_CORE
    SectionIn RO
    SetOutPath "$INSTDIR"

    ; Consume the canonical staging root directly. Do not add package/release/
    ; payload wrappers that make every installed path longer.
    File /r "${STAGE_DIR}\*.*"

    WriteUninstaller "$INSTDIR\Uninstall.exe"

    StrCmp $InstallMode "all" 0 CorePerUserRegistry

CoreAllUsersRegistry:
    WriteRegStr HKLM "${PRODUCT_REG_KEY}" "InstallDir" "$INSTDIR"
    WriteRegStr HKLM "${PRODUCT_REG_KEY}" "InstallScope" "all"
    WriteRegStr HKLM "${UNINSTALL_REG_KEY}" "DisplayName" "${PRODUCT_NAME}"
    WriteRegStr HKLM "${UNINSTALL_REG_KEY}" "DisplayVersion" "${PRODUCT_VERSION}"
    WriteRegStr HKLM "${UNINSTALL_REG_KEY}" "Publisher" "${PRODUCT_PUBLISHER}"
    WriteRegStr HKLM "${UNINSTALL_REG_KEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKLM "${UNINSTALL_REG_KEY}" "DisplayIcon" "$INSTDIR\MiaoDesk.exe"
    WriteRegStr HKLM "${UNINSTALL_REG_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
    WriteRegDWORD HKLM "${UNINSTALL_REG_KEY}" "NoModify" 1
    WriteRegDWORD HKLM "${UNINSTALL_REG_KEY}" "NoRepair" 1
    Goto CoreRegistryDone

CorePerUserRegistry:
    WriteRegStr HKCU "${PRODUCT_REG_KEY}" "InstallDir" "$INSTDIR"
    WriteRegStr HKCU "${PRODUCT_REG_KEY}" "InstallScope" "current"
    WriteRegStr HKCU "${UNINSTALL_REG_KEY}" "DisplayName" "${PRODUCT_NAME}"
    WriteRegStr HKCU "${UNINSTALL_REG_KEY}" "DisplayVersion" "${PRODUCT_VERSION}"
    WriteRegStr HKCU "${UNINSTALL_REG_KEY}" "Publisher" "${PRODUCT_PUBLISHER}"
    WriteRegStr HKCU "${UNINSTALL_REG_KEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKCU "${UNINSTALL_REG_KEY}" "DisplayIcon" "$INSTDIR\MiaoDesk.exe"
    WriteRegStr HKCU "${UNINSTALL_REG_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
    WriteRegDWORD HKCU "${UNINSTALL_REG_KEY}" "NoModify" 1
    WriteRegDWORD HKCU "${UNINSTALL_REG_KEY}" "NoRepair" 1

CoreRegistryDone:
SectionEnd

; Optional enhancement only. MiaoDesk itself must work without this Junction.
; It exists solely for old third-party code that appends additional deep paths.
Section /o "旧组件短路径兼容别名（Junction）" SEC_JUNCTION
    ; Creating a root-level alias normally needs elevation. Limited users simply
    ; skip this optional enhancement and retain the normal install tree.
    StrCmp $InstallMode "all" 0 JunctionLimitedUser

    ${GetRoot} "$INSTDIR" $InstallRoot
    ${GetRoot} "$WINDIR" $WindowsRoot

    ; Alias is intentionally on the Windows/system drive. Junctions are only
    ; enabled when the real installation is on that same drive; cross-drive
    ; installations never use this path-shortening enhancement.
    StrCmp "$InstallRoot" "$WindowsRoot" JunctionSameDisk JunctionDifferentDisk

JunctionLimitedUser:
    DetailPrint "Junction skipped: current account is using a limited per-user install."
    Goto JunctionDone

JunctionDifferentDisk:
    DetailPrint "Junction skipped: install drive $InstallRoot differs from Windows drive $WindowsRoot."
    Goto JunctionDone

JunctionSameDisk:
    StrCpy $JunctionBase "$WindowsRoot\MDJ"
    StrCpy $JunctionPath "$JunctionBase\MiaoDeskRuntime"

    IfFileExists "$INSTDIR\Runtime\*.*" JunctionRuntimeExists JunctionNoRuntime

JunctionNoRuntime:
    DetailPrint "Junction skipped: $INSTDIR\Runtime does not exist."
    Goto JunctionDone

JunctionRuntimeExists:
    CreateDirectory "$JunctionBase"
    IfErrors JunctionCreateFailed

    ; Never overwrite an existing path; it may belong to the user or another
    ; installation. This also avoids destructive cleanup attempts.
    IfFileExists "$JunctionPath\*.*" JunctionAlreadyExists JunctionCreate

JunctionAlreadyExists:
    DetailPrint "Junction alias already exists; leaving it untouched: $JunctionPath"
    Goto JunctionDone

JunctionCreate:
    nsExec::ExecToStack '"$SYSDIR\cmd.exe" /D /C mklink /J "$JunctionPath" "$INSTDIR\Runtime"'
    Pop $0
    Pop $1
    StrCmp $0 "0" JunctionCreated JunctionCreateFailed

JunctionCreated:
    DetailPrint "Short runtime junction created: $JunctionPath -> $INSTDIR\Runtime"
    WriteRegStr HKLM "${PRODUCT_REG_KEY}" "RuntimeJunction" "$JunctionPath"
    Goto JunctionDone

JunctionCreateFailed:
    DetailPrint "Junction creation failed; continuing without it."
    DetailPrint "$1"

JunctionDone:
SectionEnd

LangString DESC_SEC_CORE ${LANG_SIMPCHINESE} "安装 MiaoDesk 与 CMake install 整理后的生产运行时文件。"
LangString DESC_SEC_JUNCTION ${LANG_SIMPCHINESE} "可选：为旧组件创建同磁盘短路径 Junction；失败不会影响正常安装。"

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
    !insertmacro MUI_DESCRIPTION_TEXT ${SEC_CORE} $(DESC_SEC_CORE)
    !insertmacro MUI_DESCRIPTION_TEXT ${SEC_JUNCTION} $(DESC_SEC_JUNCTION)
!insertmacro MUI_FUNCTION_DESCRIPTION_END

Section "Uninstall"
    ; Determine where this installation registered itself. Do not rely on the
    ; current elevation state to infer original install scope.
    ReadRegStr $0 HKLM "${PRODUCT_REG_KEY}" "InstallDir"
    StrCmp $0 "$INSTDIR" UninstallAllUsers 0
    ReadRegStr $0 HKCU "${PRODUCT_REG_KEY}" "InstallDir"
    StrCmp $0 "$INSTDIR" UninstallCurrentUser UninstallCurrentUser

UninstallAllUsers:
    ReadRegStr $JunctionPath HKLM "${PRODUCT_REG_KEY}" "RuntimeJunction"
    StrCmp "$JunctionPath" "" UninstallAllNoJunction 0

    ; CRITICAL SAFETY RULE: remove only the Junction object. Never use /S and
    ; never run RMDir /r on this alias, which could otherwise risk real data.
    nsExec::ExecToLog '"$SYSDIR\cmd.exe" /D /C rmdir "$JunctionPath"'

UninstallAllNoJunction:
    DeleteRegKey HKLM "${UNINSTALL_REG_KEY}"
    DeleteRegKey HKLM "${PRODUCT_REG_KEY}"
    Goto UninstallFiles

UninstallCurrentUser:
    ; Per-user installations do not create the root-level Junction.
    DeleteRegKey HKCU "${UNINSTALL_REG_KEY}"
    DeleteRegKey HKCU "${PRODUCT_REG_KEY}"

UninstallFiles:
    ; User settings/cache/secrets live outside INSTDIR, under MiaoDesk's data
    ; locations / Windows Credential Manager. The install tree is product-owned.
    RMDir /r "$INSTDIR"

    ; Remove the optional Junction parent only if it is now empty. No /r.
    ${GetRoot} "$WINDIR" $WindowsRoot
    StrCpy $JunctionBase "$WindowsRoot\MDJ"
    RMDir "$JunctionBase"
SectionEnd
