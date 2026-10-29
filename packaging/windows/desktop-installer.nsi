Unicode true
!include "MUI2.nsh"
!include "LogicLib.nsh"
!ifndef PAYLOAD_DIR
!error "PAYLOAD_DIR must be the complete desktop build directory"
!endif
!ifndef OUTPUT_FILE
!error "OUTPUT_FILE must be the target installer path"
!endif
!ifndef REPO_DIR
!error "REPO_DIR must be the repository root"
!endif
Name "Sony Device Center"
OutFile "${OUTPUT_FILE}"
InstallDir "$LOCALAPPDATA\Programs\Sony Device Center"
InstallDirRegKey HKCU "Software\SonyDeviceCenter" "InstallDir"
RequestExecutionLevel user
SetCompressor /SOLID lzma
!define MUI_ABORTWARNING
!define MUI_ICON "${REPO_DIR}\packaging\windows\sony-device-center.ico"
!define MUI_UNICON "${REPO_DIR}\packaging\windows\sony-device-center.ico"
!define MUI_FINISHPAGE_RUN "$INSTDIR\SonyDeviceCenter.exe"
!define MUI_FINISHPAGE_RUN_TEXT "启动 Sony Device Center"
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${REPO_DIR}\LICENSE"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "SimpChinese"
!insertmacro MUI_LANGUAGE "English"

Section "Sony Device Center" Main
    SectionIn RO
    SetShellVarContext current
    SetOverwrite on
    ; Ask the current-user instance to exit before replacing runtime files.
    IfFileExists "$INSTDIR\instance-control-v1" 0 +3
    ExecWait '"$INSTDIR\SonyDeviceCenter.exe" --exit'
    Sleep 500
    SetOutPath "$INSTDIR"
    File /r /x "*.pdb" /x "hardware-*.log" /x "frontend-*.png" "${PAYLOAD_DIR}\*"
    WriteUninstaller "$INSTDIR\Uninstall.exe"
    CreateDirectory "$SMPROGRAMS\Sony Device Center"
    CreateShortCut "$SMPROGRAMS\Sony Device Center\Sony Device Center.lnk" "$INSTDIR\SonyDeviceCenter.exe" "" "$INSTDIR\SonyDeviceCenter.exe"
    CreateShortCut "$SMPROGRAMS\Sony Device Center\卸载.lnk" "$INSTDIR\Uninstall.exe"
    CreateShortCut "$DESKTOP\Sony Device Center.lnk" "$INSTDIR\SonyDeviceCenter.exe" "" "$INSTDIR\SonyDeviceCenter.exe"
    WriteRegStr HKCU "Software\SonyDeviceCenter" "InstallDir" "$INSTDIR"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\SonyDeviceCenter" "DisplayName" "Sony Device Center"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\SonyDeviceCenter" "DisplayVersion" "1.3"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\SonyDeviceCenter" "InstallLocation" "$INSTDIR"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\SonyDeviceCenter" "DisplayIcon" "$INSTDIR\SonyDeviceCenter.exe"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\SonyDeviceCenter" "UninstallString" '$\"$INSTDIR\Uninstall.exe$\"'
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\SonyDeviceCenter" "QuietUninstallString" '$\"$INSTDIR\Uninstall.exe$\" /S'
    WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\SonyDeviceCenter" "NoModify" 1
    WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\SonyDeviceCenter" "NoRepair" 1
    ; Startup is opt-in. Preserve an enabled preference on upgrade.
    ReadRegStr $0 HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "SonyDeviceCenter"
    ${If} $0 != ""
        WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "SonyDeviceCenter" '$\"$INSTDIR\SonyDeviceCenter.exe$\" --tray'
    ${EndIf}
SectionEnd

Section "Uninstall"
    SetShellVarContext current
    IfFileExists "$INSTDIR\instance-control-v1" 0 +3
    ExecWait '"$INSTDIR\SonyDeviceCenter.exe" --exit'
    Sleep 500
    DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "SonyDeviceCenter"
    DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\SonyDeviceCenter"
    DeleteRegKey HKCU "Software\SonyDeviceCenter"
    Delete "$DESKTOP\Sony Device Center.lnk"
    Delete "$SMPROGRAMS\Sony Device Center\Sony Device Center.lnk"
    Delete "$SMPROGRAMS\Sony Device Center\卸载.lnk"
    RMDir "$SMPROGRAMS\Sony Device Center"
    !include "${UNINSTALL_MANIFEST}"
    Delete "$INSTDIR\Uninstall.exe"
    RMDir "$INSTDIR"
SectionEnd
