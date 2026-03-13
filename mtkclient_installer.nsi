; mtkclient_installer.nsi - NSIS installer for mtkclient Windows
; Builds an installer EXE that includes:
;   - mtkclient CLI and GUI executables (PyInstaller)
;   - Native USB driver DLL
;   - MediaTek USB driver INF (self-signed)
;   - libusb DLLs
;
; The installer auto-installs the driver INF and self-signed certificate.

!include "MUI2.nsh"
!include "FileFunc.nsh"
!include "x64.nsh"
!include "WinMessages.nsh"
!include "WordFunc.nsh"

; --- General ---
Name "mtkclient"
OutFile "mtkclient_setup.exe"
InstallDir "$PROGRAMFILES64\mtkclient"
RequestExecutionLevel admin
Unicode True

; --- Version info ---
VIProductVersion "2.1.3.0"
VIAddVersionKey "ProductName" "mtkclient"
VIAddVersionKey "ProductVersion" "2.1.3"
VIAddVersionKey "FileVersion" "2.1.3.0"
VIAddVersionKey "FileDescription" "mtkclient - MediaTek Flash Tool for Windows"
VIAddVersionKey "LegalCopyright" "(c) B.Kerler 2018-2026 GPLv3"

; --- UI ---
!define MUI_ABORTWARNING

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "Spanish"

; --- Helper: Add directory to system PATH via registry ---
Function AddToPath
    ReadRegStr $0 HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" "Path"
    StrCpy $1 "$INSTDIR"
    ; Check if already in PATH
    ${WordFind} "$0" "$1" "E+1{" $R0
    IfErrors 0 skip_add
    ; Append to PATH
    StrCpy $0 "$0;$1"
    WriteRegExpandStr HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" "Path" "$0"
    ; Notify all windows of environment change
    SendMessage ${HWND_BROADCAST} ${WM_WININICHANGE} 0 "STR:Environment" /TIMEOUT=5000
    skip_add:
FunctionEnd

; --- Helper: Remove directory from system PATH via registry ---
Function un.RemoveFromPath
    ReadRegStr $0 HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" "Path"
    StrCpy $1 "$INSTDIR"
    ; Remove our entry (with leading or trailing semicolons)
    ${WordFind} "$0" "$1" "E+1{" $R0
    IfErrors skip_remove
    StrCpy $2 "$0" "" ""
    ${WordReplace} "$2" ";$1" "" "+" $0
    ${WordReplace} "$0" "$1;" "" "+" $0
    ${WordReplace} "$0" "$1" "" "+" $0
    WriteRegExpandStr HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" "Path" "$0"
    SendMessage ${HWND_BROADCAST} ${WM_WININICHANGE} 0 "STR:Environment" /TIMEOUT=5000
    skip_remove:
FunctionEnd

; --- Main install section ---
Section "mtkclient (required)" SecMain
    SectionIn RO
    SetOutPath "$INSTDIR"

    ; Main executables
    File /oname=mtk.exe "dist\mtk_console.exe"
    File /nonfatal /oname=mtk_gui.exe "dist\mtk_standalone.exe"

    ; Native USB driver DLL
    File /nonfatal "build_dll\bin\Release\mtk_usb_driver.dll"
    File /nonfatal "build_dll\bin\mtk_usb_driver.dll"

    ; libusb DLLs
    File "mtkclient\Windows\libusb-1.0.dll"
    File /nonfatal "mtkclient\Windows\libusb32-1.0.dll"

    ; Driver files
    SetOutPath "$INSTDIR\driver"
    File "mtkclient\Windows\driver\mtkclient_preloader.inf"
    File /nonfatal "mtkclient\Windows\driver\mtkclient_preloader.cat"
    File /nonfatal "mtkclient\Windows\driver\mtkclient_cert.cer"
    ; WinUSB driver (recommended - no UsbDk/libusb required)
    File /nonfatal "Setup\Windows\driver\mtkclient_winusb.inf"

    ; Uninstaller
    SetOutPath "$INSTDIR"
    WriteUninstaller "$INSTDIR\uninstall.exe"

    ; Start menu shortcuts
    CreateDirectory "$SMPROGRAMS\mtkclient"
    CreateShortCut "$SMPROGRAMS\mtkclient\mtkclient GUI.lnk" "$INSTDIR\mtk_gui.exe"
    CreateShortCut "$SMPROGRAMS\mtkclient\mtkclient CLI.lnk" "$INSTDIR\mtk.exe"
    CreateShortCut "$SMPROGRAMS\mtkclient\Uninstall.lnk" "$INSTDIR\uninstall.exe"

    ; Add to system PATH
    Call AddToPath

    ; Add/Remove Programs entry
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\mtkclient" \
        "DisplayName" "mtkclient - MediaTek Flash Tool"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\mtkclient" \
        "UninstallString" "$\"$INSTDIR\uninstall.exe$\""
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\mtkclient" \
        "InstallLocation" "$INSTDIR"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\mtkclient" \
        "Publisher" "mtkclient"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\mtkclient" \
        "DisplayVersion" "2.1.3"
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\mtkclient" \
        "NoModify" 1
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\mtkclient" \
        "NoRepair" 1
SectionEnd

Section "Install USB Driver" SecDriver
    ; Install self-signed certificate to TrustedPublisher store
    IfFileExists "$INSTDIR\driver\mtkclient_cert.cer" 0 skip_cert
    nsExec::ExecToLog 'certutil -addstore "TrustedPublisher" "$INSTDIR\driver\mtkclient_cert.cer"'
    nsExec::ExecToLog 'certutil -addstore "Root" "$INSTDIR\driver\mtkclient_cert.cer"'
    skip_cert:

    ; Install WinUSB driver (preferred - enables direct USB access)
    IfFileExists "$INSTDIR\driver\mtkclient_winusb.inf" 0 skip_winusb
    nsExec::ExecToLog 'pnputil /add-driver "$INSTDIR\driver\mtkclient_winusb.inf" /install'
    skip_winusb:

    ; Install serial port driver (fallback)
    nsExec::ExecToLog 'pnputil /add-driver "$INSTDIR\driver\mtkclient_preloader.inf" /install'
SectionEnd

Section "Uninstall"
    ; Try to remove drivers
    nsExec::ExecToLog 'pnputil /delete-driver "$INSTDIR\driver\mtkclient_winusb.inf" /uninstall'
    nsExec::ExecToLog 'pnputil /delete-driver "$INSTDIR\driver\mtkclient_preloader.inf" /uninstall'

    ; Remove from PATH
    Call un.RemoveFromPath

    ; Remove files
    Delete "$INSTDIR\mtk.exe"
    Delete "$INSTDIR\mtk_gui.exe"
    Delete "$INSTDIR\mtk_usb_driver.dll"
    Delete "$INSTDIR\libusb-1.0.dll"
    Delete "$INSTDIR\libusb32-1.0.dll"
    Delete "$INSTDIR\driver\mtkclient_preloader.inf"
    Delete "$INSTDIR\driver\mtkclient_preloader.cat"
    Delete "$INSTDIR\driver\mtkclient_cert.cer"
    Delete "$INSTDIR\driver\mtkclient_winusb.inf"
    RMDir "$INSTDIR\driver"
    Delete "$INSTDIR\uninstall.exe"
    RMDir "$INSTDIR"

    ; Remove shortcuts
    Delete "$SMPROGRAMS\mtkclient\mtkclient GUI.lnk"
    Delete "$SMPROGRAMS\mtkclient\mtkclient CLI.lnk"
    Delete "$SMPROGRAMS\mtkclient\Uninstall.lnk"
    RMDir "$SMPROGRAMS\mtkclient"

    ; Remove registry
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\mtkclient"
SectionEnd
