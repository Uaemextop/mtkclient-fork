# MediaTek USB Drivers for Windows 10/11 x64

These drivers are derived from the **MTK SP Drivers 20160804** package and
repackaged for **Windows 10 / 11 (x64, build 22000+)** compatibility.

## What changed from the original SP Drivers

| Original (2016)            | Repackaged (2026)                   |
|----------------------------|--------------------------------------|
| `usb2ser.sys` (custom)     | `usbser.sys` (inbox since Win 10)    |
| WinUSB CoInstallers (DLLs) | Inbox WinUSB (no extra files)        |
| `DefaultDestDir=12`        | `DefaultDestDir=13` (Driver Store)   |
| No `PnpLockdown`           | `PnpLockdown=1` (DCH compliant)     |
| NTamd64 only               | NTamd64 (Win10+ inbox compatible)    |
| Needed WdfCoInstaller DLLs | No CoInstallers needed               |

## Files

| File                          | Purpose                                     |
|-------------------------------|----------------------------------------------|
| `mtkclient_winusb.inf`        | WinUSB driver for BROM, Preloader, DA, ADB   |
| `mtkclient_preloader.inf`     | Serial COM port driver for VCOM/Meta modes   |
| `install_drivers.bat`         | One-click driver installation (Run as Admin)  |

## Supported Device Modes (VID 0x0E8D)

### WinUSB driver (`mtkclient_winusb.inf`)
Used by **mtkclient** for direct USB bulk communication.

| PID      | Mode                 |
|----------|----------------------|
| `0x0003` | **BROM** (BootROM)   |
| `0x2000` | **Preloader**        |
| `0x20FF` | Preloader (alt)      |
| `0x3000` | Preloader (alt)      |
| `0x6000` | Preloader (alt)      |
| `0x2001` | **DA** (Download Agent) |
| `0x2024` | Fastboot / Bootloader |
| + ADB composite interfaces from original SP Drivers |

### Serial driver (`mtkclient_preloader.inf`)
For VCOM / diagnostic COM port enumeration.

| PID      | Mode                       |
|----------|----------------------------|
| `0x0003` | BROM VCOM                  |
| `0x2000` | Preloader VCOM             |
| `0x2001` | DA VCOM                    |
| `0x2007` | Meta VCOM                  |
| + All composite VCOM/ETS/ELT/Modem/AT from original SP Drivers |

## Installation

### Option 1 — Run the batch script (recommended)

1. Right-click `install_drivers.bat` → **Run as administrator**
2. The script installs both INF files using `pnputil`

### Option 2 — Manual installation via Device Manager

1. Open **Device Manager**
2. Find the unknown MediaTek device (yellow exclamation mark)
3. Right-click → **Update driver** → **Browse my computer**
4. Point to this `driver` folder
5. Confirm installation

### Option 3 — Command line

```powershell
# Run as Administrator
pnputil /add-driver mtkclient_winusb.inf /install
pnputil /add-driver mtkclient_preloader.inf /install
```

## Test-signing (for unsigned drivers)

On Windows 11, drivers must be signed. For test/development use:

```powershell
# Enable test signing (requires reboot)
bcdedit /set testsigning on

# Or use a self-signed certificate:
$cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject "CN=mtkclient Test"
New-FileCatalog -Path . -CatalogFilePath mtkclient_winusb.cat -CatalogVersion 2.0
New-FileCatalog -Path . -CatalogFilePath mtkclient_preloader.cat -CatalogVersion 2.0
Set-AuthenticodeSignature mtkclient_winusb.cat -Certificate $cert
Set-AuthenticodeSignature mtkclient_preloader.cat -Certificate $cert
```

## Compatibility

- **Windows 10** 1803+ (build 17134+)
- **Windows 11** all versions (build 22000+)
- **Architecture**: x64 (amd64) only
- No third-party `.sys` or `.dll` files required — uses only inbox Windows drivers
