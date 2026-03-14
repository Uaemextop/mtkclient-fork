; ============================================================================
; MTK USB Serial Driver — Inno Setup Installer Script
; Builds a single .exe installer for Windows 11 x64
; Note: Windows 11 (and WDK 10.0.26100) only supports x64 kernel-mode drivers
; ============================================================================

#define AppName      "MTK Preloader USB Serial Driver"
#define AppVersion   "1.0.0"
#define AppPublisher "MTK Loader Drivers Opensource"
#define AppURL       "https://github.com/Uaemextop/mtk-loader-drivers-opensource-win11"

[Setup]
AppId={{E8D00003-MTK0-USB0-VCOM-PRELOADER001}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
DefaultDirName={autopf}\MTK USB Driver
DefaultGroupName={#AppName}
OutputBaseFilename=MTK_USB_Driver_Setup_{#AppVersion}
Compression=lzma2/ultra64
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64compatible
ArchitecturesAllowed=x64compatible
MinVersion=10.0.22000
PrivilegesRequired=admin
SetupIconFile=compiler:SetupClassicIcon.ico
UninstallDisplayName={#AppName}
LicenseFile=..\LICENSE
OutputDir=output

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"

[Messages]
english.WelcomeLabel2=This will install the MediaTek USB Serial driver on your computer.%n%nThis driver enables communication with MTK devices in Preloader, BROM, Download Agent (DA), and Meta modes.%n%nCompatible with SP Flash Tool and mtkclient.
spanish.WelcomeLabel2=Este instalador instalara el driver USB Serial de MediaTek en su computadora.%n%nEste driver habilita la comunicacion con dispositivos MTK en modos Preloader, BROM, Download Agent (DA) y Meta.%n%nCompatible con SP Flash Tool y mtkclient.

[Files]
; ── x64 driver binaries ──────────────────────────────────────────────────────
Source: "build\x64\Release\mtk_usb2ser.sys";  DestDir: "{app}\x64"; Flags: ignoreversion
Source: "build\x64\Release\mtk_usb2ser.inf";  DestDir: "{app}";    Flags: ignoreversion
Source: "build\x64\Release\mtk_usb2ser.cat";  DestDir: "{app}";    Flags: ignoreversion skipifsourcedoesntexist
Source: "build\x64\Release\mtk_usb2ser.pdb";  DestDir: "{app}\x64"; Flags: ignoreversion skipifsourcedoesntexist
Source: "build\x64\Release\WdfCoInstaller01033.dll"; DestDir: "{app}\x64"; Flags: ignoreversion skipifsourcedoesntexist

; ── Test code-signing certificate (DER binary .cer) ─────────────────────────
; Exported automatically by the CI build.  Missing on production-cert builds.
Source: "build\x64\Release\mtk_test_cert.cer"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist

; ── Helper scripts ───────────────────────────────────────────────────────────
Source: "install_driver.ps1";   DestDir: "{app}"; Flags: ignoreversion
Source: "uninstall_driver.ps1"; DestDir: "{app}"; Flags: ignoreversion

; ── Documentation ────────────────────────────────────────────────────────────
Source: "..\..\..\..\README.md";  DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\docs\INSTALL.md";     DestDir: "{app}\docs"; Flags: ignoreversion

[Run]
; Step 1: Install test code-signing certificate into Trusted Root CA
;         (only when the .cer file is present — skipped for production builds)
Filename: "certutil.exe"; \
    Parameters: "-addstore Root ""{app}\mtk_test_cert.cer"""; \
    StatusMsg: "Installing code-signing certificate (Trusted Root CA)..."; \
    Flags: runhidden waituntilterminated; \
    Check: CertFileExists

; Step 2: Install test certificate into Trusted Publishers
Filename: "certutil.exe"; \
    Parameters: "-addstore TrustedPublisher ""{app}\mtk_test_cert.cer"""; \
    StatusMsg: "Installing code-signing certificate (Trusted Publishers)..."; \
    Flags: runhidden waituntilterminated; \
    Check: CertFileExists

; Step 3: Enable Windows Test Signing mode
;         Required for drivers signed with a self-signed test certificate.
;         Not needed when using an EV production certificate.
Filename: "bcdedit.exe"; \
    Parameters: "/set testsigning on"; \
    StatusMsg: "Enabling Windows Test Signing mode..."; \
    Flags: runhidden waituntilterminated; \
    Check: CertFileExists

; Step 4: Install driver via pnputil
Filename: "powershell.exe"; \
    Parameters: "-ExecutionPolicy Bypass -File ""{app}\install_driver.ps1"" -InfPath ""{app}\mtk_usb2ser.inf"""; \
    StatusMsg: "Installing MediaTek USB Serial driver..."; \
    Flags: runhidden waituntilterminated; \
    Check: ShouldInstallDriver

[UninstallRun]
Filename: "powershell.exe"; \
    Parameters: "-ExecutionPolicy Bypass -File ""{app}\uninstall_driver.ps1"""; \
    Flags: runhidden waituntilterminated

[Code]
{ Returns True when the test certificate file is bundled (CI/test builds) }
function CertFileExists: Boolean;
begin
  Result := FileExists(ExpandConstant('{app}\mtk_test_cert.cer'));
end;

function ShouldInstallDriver: Boolean;
begin
  Result := True;
end;

function InitializeSetup(): Boolean;
begin
  if not IsAdmin then
  begin
    MsgBox('This installer requires administrator privileges.' + #13#10 +
           'Please right-click and select "Run as administrator".', mbError, MB_OK);
    Result := False;
    Exit;
  end;
  Result := True;
end;

{ After installation, inform the user that a reboot is needed when Test Signing
  was enabled (test-cert builds only). }
procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssDone then
  begin
    if CertFileExists then
    begin
      MsgBox(
        'Installation complete.' + #13#10 + #13#10 +
        'IMPORTANT: Windows Test Signing mode has been enabled.' + #13#10 +
        'A REBOOT is required before the driver will load.' + #13#10 + #13#10 +
        'After rebooting you will see "Test Mode" in the bottom-right' + #13#10 +
        'corner of the desktop — this is expected for test-signed drivers.' + #13#10 + #13#10 +
        'Connect your MTK device after rebooting.  A new COM port will' + #13#10 +
        'appear in Device Manager.',
        mbInformation, MB_OK);
    end;
  end;
end;
