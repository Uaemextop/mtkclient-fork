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
VIAddVersionKey "FileDescription" "mtkclient - MediaTek Flash Tool for Windows"
VIAddVersionKey "LegalCopyright" "(c) B.Kerler 2018-2026 GPLv3"

; --- UI ---
!define MUI_ICON "mtkclient\icon.ico"
!define MUI_ABORTWARNING

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "Spanish"

; --- Sections ---
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

    ; Driver INF
    SetOutPath "$INSTDIR\driver"
    File "mtkclient\Windows\driver\mtkclient_preloader.inf"
    File /nonfatal "mtkclient\Windows\driver\mtkclient_preloader.cat"

    ; Uninstaller
    SetOutPath "$INSTDIR"
    WriteUninstaller "$INSTDIR\uninstall.exe"

    ; Start menu
    CreateDirectory "$SMPROGRAMS\mtkclient"
    CreateShortCut "$SMPROGRAMS\mtkclient\mtkclient GUI.lnk" "$INSTDIR\mtk_gui.exe"
    CreateShortCut "$SMPROGRAMS\mtkclient\mtkclient CLI.lnk" "$INSTDIR\mtk.exe"
    CreateShortCut "$SMPROGRAMS\mtkclient\Uninstall.lnk" "$INSTDIR\uninstall.exe"

    ; Add to PATH
    EnVar::SetHKLM
    EnVar::AddValue "PATH" "$INSTDIR"

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
    ; Install the self-signed certificate to TrustedPublisher store
    ; (the cert was created and embedded in the .cat during build)
    nsExec::ExecToLog 'certutil -addstore "TrustedPublisher" "$INSTDIR\driver\mtkclient_preloader.cat"'

    ; Install the driver INF using pnputil
    nsExec::ExecToLog 'pnputil /add-driver "$INSTDIR\driver\mtkclient_preloader.inf" /install'

    ; Fallback: also try the older method
    nsExec::ExecToLog 'rundll32.exe setupapi.dll,InstallHinfSection DefaultInstall 132 $INSTDIR\driver\mtkclient_preloader.inf'
SectionEnd

Section "Uninstall"
    ; Remove driver
    nsExec::ExecToLog 'pnputil /delete-driver "$INSTDIR\driver\mtkclient_preloader.inf" /uninstall'

    ; Remove from PATH
    EnVar::SetHKLM
    EnVar::DeleteValue "PATH" "$INSTDIR"

    ; Remove files
    Delete "$INSTDIR\mtk.exe"
    Delete "$INSTDIR\mtk_gui.exe"
    Delete "$INSTDIR\mtk_usb_driver.dll"
    Delete "$INSTDIR\libusb-1.0.dll"
    Delete "$INSTDIR\libusb32-1.0.dll"
    Delete "$INSTDIR\driver\mtkclient_preloader.inf"
    Delete "$INSTDIR\driver\mtkclient_preloader.cat"
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
