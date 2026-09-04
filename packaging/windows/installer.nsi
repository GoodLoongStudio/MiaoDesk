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

; Microsoft Partner Center reads the package version from the installer's Windows
; version resource. Without VIProductVersion the EXE reports version 0.0.0.0 and the
; Store submission cannot be versioned. VIProductVersion requires four components.
!define PRODUCT_VERSION_QUAD "${PRODUCT_VERSION}.0"

Name "${PRODUCT_NAME}"
OutFile "${OUTPUT_FILE}"

VIProductVersion "${PRODUCT_VERSION_QUAD}"
VIFileVersion "${PRODUCT_VERSION_QUAD}"
VIAddVersionKey "ProductName" "${PRODUCT_NAME}"
VIAddVersionKey "ProductVersion" "${PRODUCT_VERSION}"
VIAddVersionKey "FileVersion" "${PRODUCT_VERSION}"
VIAddVersionKey "CompanyName" "${PRODUCT_PUBLISHER}"
VIAddVersionKey "FileDescription" "${PRODUCT_NAME} ${PRODUCT_VERSION} Installer"
VIAddVersionKey "LegalCopyright" "Copyright (c) GoodLoongStudio"
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

    ; A previous MiaoDesk release may already own the goz service. Stop it
    ; before replacing the executable because Windows keeps service images
    ; locked while they are running.
    StrCmp $InstallMode "all" 0 GozUpgradeStopped
    IfFileExists "$INSTDIR\Goz\gozd.exe" 0 GozUpgradeStopped
    nsExec::ExecToLog '"$INSTDIR\Goz\gozd.exe" uninstall'
    Pop $0
GozUpgradeStopped:
    Sleep 500

    SetOutPath "$INSTDIR"
    File /r "${STAGE_DIR}\*.*"
    WriteUninstaller "$INSTDIR\Uninstall.exe"

    ; File search is client/server: goz.exe only queries the elevated gozd
    ; indexer. Shipping both binaries without registering the daemon leaves the
    ; search UI permanently disconnected after a normal installation.
    StrCmp $InstallMode "all" 0 SkipGozServiceInstall
    nsExec::ExecToLog '"$INSTDIR\Goz\gozd.exe" install'
    Pop $0
    StrCmp $0 "0" SkipGozServiceInstall
    SetErrorLevel 5
    Abort "无法安装文件搜索索引服务（gozd 返回 $0）。"
SkipGozServiceInstall:

    ; Recreate shortcut files instead of updating them in place. Explorer may
    ; otherwise keep the previous shortcut/icon metadata when an application is
    ; reinstalled to the same path with the same icon filename.
    Delete "$SMPROGRAMS\MiaoDesk.lnk"
    Delete "$DESKTOP\MiaoDesk.lnk"
    CreateShortCut "$SMPROGRAMS\MiaoDesk.lnk" "$INSTDIR\MiaoDesk.exe" "" "$INSTDIR\Assets\MiaoMiao.ico" 0
    CreateShortCut "$DESKTOP\MiaoDesk.lnk" "$INSTDIR\MiaoDesk.exe" "" "$INSTDIR\Assets\MiaoMiao.ico" 0

    ; Tell Explorer that shell/icon metadata changed so an in-place upgrade does
    ; not keep rendering a cached icon from the previous MiaoDesk installation.
    System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'

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
    StrCpy $InstallMode "all"
    SetShellVarContext all
    DeleteRegKey HKLM "${UNINSTALL_REG_KEY}"
    DeleteRegKey HKLM "${PRODUCT_REG_KEY}"
    Goto RemoveFiles

PerUserUninstall:
    StrCpy $InstallMode "current"
    SetShellVarContext current
    DeleteRegKey HKCU "${UNINSTALL_REG_KEY}"
    DeleteRegKey HKCU "${PRODUCT_REG_KEY}"

RemoveFiles:
    ; Stop and unregister the indexer before deleting its executable. This is
    ; also required for upgrades because Windows locks a running service image.
    StrCmp $InstallMode "all" 0 GozServiceRemoved
    IfFileExists "$INSTDIR\Goz\gozd.exe" 0 GozServiceRemoved
    nsExec::ExecToLog '"$INSTDIR\Goz\gozd.exe" uninstall'
    Pop $0
GozServiceRemoved:
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
