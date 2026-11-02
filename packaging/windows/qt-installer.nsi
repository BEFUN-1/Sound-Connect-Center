Unicode true
!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "FileFunc.nsh"

!ifndef PAYLOAD_DIR
!error "PAYLOAD_DIR must be the complete Qt install tree"
!endif
!ifndef OUTPUT_FILE
!error "OUTPUT_FILE must be the target installer path"
!endif
!ifndef REPO_DIR
!error "REPO_DIR must be the repository root"
!endif
!ifndef VERSION
!error "VERSION must be the Qt release version"
!endif
!ifndef UNINSTALL_MANIFEST
!error "UNINSTALL_MANIFEST must list the staged payload files"
!endif

Name "Sony Device Center"
OutFile "${OUTPUT_FILE}"
InstallDir "$LOCALAPPDATA\Programs\Sony Device Center"
RequestExecutionLevel user
SetCompressor /SOLID lzma

!define MUI_ABORTWARNING
!define MUI_ICON "${REPO_DIR}\packaging\windows\sony-device-center.ico"
!define MUI_UNICON "${REPO_DIR}\packaging\windows\sony-device-center.ico"
!define MUI_FINISHPAGE_RUN "$INSTDIR\bin\sony-device-center.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Launch Sony Device Center"
!define MUI_FINISHPAGE_RUN_NOTCHECKED
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${REPO_DIR}\LICENSE"
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

!macro DefineInstallGuards Prefix
Function ${Prefix}RequireUserToken
    ; asInvoker also inherits a high token when explicitly run as administrator.
    ; Refuse that token before reading or modifying any user-controlled path.
    System::Call 'kernel32::GetCurrentProcess() p .r0'
    System::Call 'advapi32::OpenProcessToken(p r0, i 8, *p .r1) i .r2'
    ${If} $2 == 0
        Goto token_error
    ${EndIf}
    System::Call 'advapi32::GetTokenInformation(p r1, i 20, *i .r2, i 4, *i .r3) i .r0'
    System::Call 'kernel32::CloseHandle(p r1)'
    ${If} $0 == 0
        Goto token_error
    ${EndIf}
    ${If} $2 != 0
        Goto token_error
    ${EndIf}
    Return
token_error:
    SetErrorLevel 740
    MessageBox MB_OK|MB_ICONSTOP "Run this per-user installer or uninstaller normally, without administrator privileges." /SD IDOK
    Quit
FunctionEnd

Function ${Prefix}ValidateTree
    Exch $0
    Push $1
    Push $2
    Push $3
    FindFirst $1 $2 "$0\*"
    ${DoWhile} $2 != ""
        ${If} $2 != "."
        ${AndIf} $2 != ".."
            System::Call 'kernel32::GetFileAttributesW(w "$0\$2") i .r3'
            ${If} $3 == -1
                Goto tree_error
            ${EndIf}
            IntOp $3 $3 & 0x400
            ${If} $3 != 0
                Goto tree_error
            ${EndIf}
            IfFileExists "$0\$2\*.*" 0 +3
            Push "$0\$2"
            Call ${Prefix}ValidateTree
        ${EndIf}
        FindNext $1 $2
    ${Loop}
    FindClose $1
    Pop $3
    Pop $2
    Pop $1
    Pop $0
    Return
tree_error:
    SetErrorLevel 2
    MessageBox MB_OK|MB_ICONSTOP "The installation contains an unreadable path, symbolic link or directory junction. Remove it before continuing." /SD IDOK
    Quit
FunctionEnd

Function ${Prefix}ValidateInstallDir
    ${If} $INSTDIR == ""
    ${OrIf} $LOCALAPPDATA == ""
        Goto invalid_directory
    ${EndIf}
    System::Call 'kernel32::GetFullPathNameW(w "$INSTDIR", i ${NSIS_MAX_STRLEN}, w .r0, p 0) i .r2'
    ${If} $2 == 0
    ${OrIf} $2 >= ${NSIS_MAX_STRLEN}
    ${OrIf} $0 == ""
        Goto invalid_directory
    ${EndIf}
    System::Call 'kernel32::GetFullPathNameW(w "$LOCALAPPDATA\Programs\Sony Device Center", i ${NSIS_MAX_STRLEN}, w .r1, p 0) i .r2'
    ${If} $2 == 0
    ${OrIf} $2 >= ${NSIS_MAX_STRLEN}
    ${OrIf} $1 == ""
        Goto invalid_directory
    ${EndIf}
    ${If} $0 != $1
        Goto invalid_directory
    ${EndIf}
    StrCpy $INSTDIR $1
    ; Check every existing ancestor as well as the existing payload tree.
    ${DoWhile} $0 != ""
        System::Call 'kernel32::GetFileAttributesW(w r0) i .r2'
        ${If} $2 != -1
            IntOp $2 $2 & 0x400
            ${If} $2 != 0
                SetErrorLevel 2
                MessageBox MB_OK|MB_ICONSTOP "Installation through a symbolic link or directory junction is not supported." /SD IDOK
                Quit
            ${EndIf}
        ${EndIf}
        ${GetParent} "$0" $3
        ${If} $3 == $0
            ${Break}
        ${EndIf}
        StrCpy $0 $3
    ${Loop}
    Push "$INSTDIR"
    Call ${Prefix}ValidateTree
    Return
invalid_directory:
    SetErrorLevel 2
    MessageBox MB_OK|MB_ICONSTOP "Only the default per-user installation directory is supported. Invalid or custom /D paths are refused." /SD IDOK
    Quit
FunctionEnd
!macroend

!insertmacro DefineInstallGuards ""
!insertmacro DefineInstallGuards "un."

Function .onInit
    SetShellVarContext current
    Call RequireUserToken
    Call ValidateInstallDir
FunctionEnd

Function un.onInit
    SetShellVarContext current
    Call un.RequireUserToken
    Call un.ValidateInstallDir
FunctionEnd

Section "Sony Device Center" Main
    SectionIn RO
    SetShellVarContext current
    SetOverwrite on
    Call RequireUserToken
    Call ValidateInstallDir

    ; Replace links from older installations so the current executable and
    ; embedded application icon are always used.
    Delete "$DESKTOP\Sony Device Center.lnk"
    Delete "$SMPROGRAMS\Sony Device Center\Sony Device Center.lnk"

    SetOutPath "$INSTDIR"
    File /r /x "*.pdb" /x "hardware-*.log" /x "frontend-*.png" "${PAYLOAD_DIR}\*"

    WriteUninstaller "$INSTDIR\Uninstall.exe"
    CreateDirectory "$SMPROGRAMS\Sony Device Center"
    CreateShortCut "$SMPROGRAMS\Sony Device Center\Sony Device Center.lnk" "$INSTDIR\bin\sony-device-center.exe" "" "$INSTDIR\bin\sony-device-center.exe" 0 SW_SHOWNORMAL "" "Sony Device Center"
    CreateShortCut "$SMPROGRAMS\Sony Device Center\Uninstall.lnk" "$INSTDIR\Uninstall.exe"
    CreateShortCut "$DESKTOP\Sony Device Center.lnk" "$INSTDIR\bin\sony-device-center.exe" "" "$INSTDIR\bin\sony-device-center.exe" 0 SW_SHOWNORMAL "" "Sony Device Center"

    WriteRegStr HKCU "Software\SonyDeviceCenter" "InstallDir" "$INSTDIR"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\SonyDeviceCenter" "DisplayName" "Sony Device Center"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\SonyDeviceCenter" "DisplayVersion" "${VERSION}"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\SonyDeviceCenter" "InstallLocation" "$INSTDIR"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\SonyDeviceCenter" "DisplayIcon" "$INSTDIR\bin\sony-device-center.exe"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\SonyDeviceCenter" "UninstallString" '$\"$INSTDIR\Uninstall.exe$\"'
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\SonyDeviceCenter" "QuietUninstallString" '$\"$INSTDIR\Uninstall.exe$\" /S'
    WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\SonyDeviceCenter" "NoModify" 1
    WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\SonyDeviceCenter" "NoRepair" 1

    ; Keep an enabled startup preference, but point it at the Qt executable.
    ReadRegStr $0 HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "SonyDeviceCenter"
    ${If} $0 != ""
        WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "SonyDeviceCenter" '$\"$INSTDIR\bin\sony-device-center.exe$\" --tray'
    ${EndIf}
SectionEnd

Section "Uninstall"
    SetShellVarContext current
    Call un.RequireUserToken
    Call un.ValidateInstallDir

    DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "SonyDeviceCenter"
    DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\SonyDeviceCenter"
    DeleteRegKey HKCU "Software\SonyDeviceCenter"
    Delete "$DESKTOP\Sony Device Center.lnk"
    Delete "$SMPROGRAMS\Sony Device Center\Sony Device Center.lnk"
    Delete "$SMPROGRAMS\Sony Device Center\Uninstall.lnk"
    RMDir "$SMPROGRAMS\Sony Device Center"
    !include "${UNINSTALL_MANIFEST}"
    Delete "$INSTDIR\Uninstall.exe"
    RMDir "$INSTDIR"
SectionEnd
