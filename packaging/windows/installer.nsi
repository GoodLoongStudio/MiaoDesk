; MiaoDesk Windows installer
; Build:
;   makensis /DSTAGE_DIR="C:\pkg\MiaoDesk\x64" /DOUTPUT_FILE="MiaoDesk-x64-Setup.exe" packaging\windows\installer.nsi
;
; STAGE_DIR must be the canonical tree produced by packaging\windows\stage.ps1 -Architecture x64.

Unicode true

!include "MUI2.nsh"

!ifndef STAGE_DIR
    !error "STAGE_DIR is required."
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
InstallDir "$PROGRAMFILES64\MiaoDesk"
RequestExecutionLevel highest
SetCompressor /SOLID lzma
ShowInstDetails show
ShowUninstDetails show

Var InstallMode

!insertmacro MUI_PAGE_WELCOME
!define MUI_PAGE_CUSTOMFUNCTION_LEAVE CheckInstallDirectoryLength
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH
!insertmacro MUI_LANGUAGE "SimpChinese"

Function .onInit
    SetRegView 64
    UserInfo::GetAccountType
    Pop $0
    StrCmp $0 "Admin" 0 LimitedUser
    StrCpy $InstallMode "all"
    SetShellVarContext all
    Return

LimitedUser:
    StrCpy $InstallMode "current"
    SetShellVarContext current
    StrCpy $INSTDIR "$LOCALAPPDATA\MiaoDesk"
FunctionEnd

Function CheckInstallDirectoryLength
    StrLen $0 "$INSTDIR"
    IntCmp $0 85 Done Done Warn
Warn:
    MessageBox MB_OK|MB_ICONEXCLAMATION \
        "当前安装路径长度为 $0 个字符。建议使用较短路径，例如 C:\Program Files\MiaoDesk 或 D:\Apps\MiaoDesk。安装不会被阻止。"
Done:
FunctionEnd

Section "MiaoDesk"
    ; An in-place upgrade must not leave the previous desktop runtime and its
    ; Widget helpers alive. Otherwise the new UI talks to old in-memory code
    ; even though the files on disk were replaced.
    nsExec::ExecToLog '"$SYSDIR\taskkill.exe" /F /T /IM MiaoDesk.exe'
    Pop $0
    nsExec::ExecToLog '"$SYSDIR\taskkill.exe" /F /T /IM MiaoDeskWallpaper.exe'
    Pop $0
    nsExec::ExecToLog '"$SYSDIR\taskkill.exe" /F /T /IM MiaoDeskHarness.exe'
    Pop $0
    Sleep 500

    SetOutPath "$INSTDIR"
    File /r "${STAGE_DIR}\*.*"
    WriteUninstaller "$INSTDIR\Uninstall.exe"
    CreateShortCut "$SMPROGRAMS\MiaoDesk.lnk" "$INSTDIR\MiaoDesk.exe" "" "$INSTDIR\Assets\MiaoMiao.ico" 0
    CreateShortCut "$DESKTOP\MiaoDesk.lnk" "$INSTDIR\MiaoDesk.exe" "" "$INSTDIR\Assets\MiaoMiao.ico" 0

    StrCmp $InstallMode "all" 0 PerUser
    WriteRegStr HKLM "${PRODUCT_REG_KEY}" "InstallDir" "$INSTDIR"
    WriteRegStr HKLM "${UNINSTALL_REG_KEY}" "DisplayName" "${PRODUCT_NAME}"
    WriteRegStr HKLM "${UNINSTALL_REG_KEY}" "DisplayVersion" "${PRODUCT_VERSION}"
    WriteRegStr HKLM "${UNINSTALL_REG_KEY}" "Publisher" "${PRODUCT_PUBLISHER}"
    WriteRegStr HKLM "${UNINSTALL_REG_KEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKLM "${UNINSTALL_REG_KEY}" "DisplayIcon" "$INSTDIR\MiaoDesk.exe"
    WriteRegStr HKLM "${UNINSTALL_REG_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
    WriteRegDWORD HKLM "${UNINSTALL_REG_KEY}" "NoModify" 1
    WriteRegDWORD HKLM "${UNINSTALL_REG_KEY}" "NoRepair" 1
    Goto Done

PerUser:
    WriteRegStr HKCU "${PRODUCT_REG_KEY}" "InstallDir" "$INSTDIR"
    WriteRegStr HKCU "${UNINSTALL_REG_KEY}" "DisplayName" "${PRODUCT_NAME}"
    WriteRegStr HKCU "${UNINSTALL_REG_KEY}" "DisplayVersion" "${PRODUCT_VERSION}"
    WriteRegStr HKCU "${UNINSTALL_REG_KEY}" "Publisher" "${PRODUCT_PUBLISHER}"
    WriteRegStr HKCU "${UNINSTALL_REG_KEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKCU "${UNINSTALL_REG_KEY}" "DisplayIcon" "$INSTDIR\MiaoDesk.exe"
    WriteRegStr HKCU "${UNINSTALL_REG_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
    WriteRegDWORD HKCU "${UNINSTALL_REG_KEY}" "NoModify" 1
    WriteRegDWORD HKCU "${UNINSTALL_REG_KEY}" "NoRepair" 1
Done:
SectionEnd

Section "Uninstall"
    SetRegView 64
    ReadRegStr $0 HKLM "${PRODUCT_REG_KEY}" "InstallDir"
    StrCmp $0 "$INSTDIR" 0 PerUserUninstall
    SetShellVarContext all
    DeleteRegKey HKLM "${UNINSTALL_REG_KEY}"
    DeleteRegKey HKLM "${PRODUCT_REG_KEY}"
    Goto RemoveFiles

PerUserUninstall:
    SetShellVarContext current
    DeleteRegKey HKCU "${UNINSTALL_REG_KEY}"
    DeleteRegKey HKCU "${PRODUCT_REG_KEY}"

RemoveFiles:
    Delete "$SMPROGRAMS\MiaoDesk.lnk"
    Delete "$DESKTOP\MiaoDesk.lnk"
    Delete "$INSTDIR\MiaoDesk.exe"
    Delete "$INSTDIR\MiaoDeskWallpaper.exe"
    Delete "$INSTDIR\MiaoDeskHarness.exe"
    Delete "$INSTDIR\Uninstall.exe"

    ; Delete only product-owned subtrees. Never recursively delete an arbitrary
    ; user-selected install root.
    RMDir /r "$INSTDIR\Runtime"
    RMDir /r "$INSTDIR\AI"
    RMDir /r "$INSTDIR\Pi"
    RMDir /r "$INSTDIR\Goz"
    RMDir /r "$INSTDIR\Assets"
    RMDir /r "$INSTDIR\Config"
    RMDir /r "$INSTDIR\Wallpapers"
    RMDir "$INSTDIR"
SectionEnd
