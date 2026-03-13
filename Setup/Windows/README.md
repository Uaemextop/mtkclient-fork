# mtkclient WinUSB Driver for Windows

## Overview

This directory contains an **open-source WinUSB driver** for MediaTek bootrom
and preloader USB devices.  It replaces the need for the third-party **UsbDk**
driver on Windows 10/11 (x64).

The driver uses Microsoft's built-in **WinUSB** generic driver, which allows
`libusb` (and therefore `pyusb` / `mtkclient`) to communicate directly with
MediaTek USB devices without any additional kernel-mode components.

## Supported Devices

| VID      | PID      | Description        |
|----------|----------|--------------------|
| `0x0E8D` | `0x0003` | MTK Bootrom (BROM) |
| `0x0E8D` | `0x2000` | MTK Preloader      |
| `0x0E8D` | `0x2001` | MTK Preloader      |
| `0x0E8D` | `0x20FF` | MTK Preloader      |
| `0x0E8D` | `0x3000` | MTK Preloader      |
| `0x0E8D` | `0x6000` | MTK Preloader      |
| `0x1004` | `0x6000` | LG Preloader       |
| `0x22D9` | `0x0006` | OPPO Preloader     |

## Quick Install (Manual)

1. Open a **Command Prompt as Administrator**
2. Navigate to this directory:
   ```
   cd Setup\Windows
   ```
3. Run the installer script:
   ```
   install_driver.bat
   ```
4. Connect your MediaTek device — mtkclient will detect it automatically.

## Uninstall

Run `uninstall_driver.bat` as Administrator.

## MSI Installer

For a standard Windows installer experience, you can build an MSI package:

### Prerequisites

- [WiX Toolset v3](https://wixtoolset.org/releases/) (`candle.exe` and
  `light.exe` on PATH, or `WIX` environment variable set)
- Windows SDK (for `inf2cat.exe` and `signtool.exe` if signing the driver)

### Building the MSI

1. *(Optional)* Generate a proper catalog file:
   ```
   inf2cat /driver:driver /os:10_X64
   ```
2. *(Optional)* Sign the catalog for driver signature enforcement:
   ```
   signtool sign /a /t http://timestamp.digicert.com driver\mtkclient.cat
   ```
3. Build the MSI:
   ```
   build_msi.bat
   ```
4. The output will be at `output\mtkclient_driver.msi`.

### Installing the MSI

Double-click `mtkclient_driver.msi`, or from the command line:

```
msiexec /i mtkclient_driver.msi
```

To install silently:

```
msiexec /i mtkclient_driver.msi /quiet
```

## Directory Structure

```
Setup/Windows/
├── driver/
│   ├── mtkclient.inf         # WinUSB driver INF file
│   └── mtkclient.cat         # Driver catalog (placeholder; sign for production)
├── installer/
│   └── mtkclient_driver.wxs  # WiX XML source for MSI package
├── install_driver.bat        # Quick-install script (Administrator)
├── uninstall_driver.bat      # Uninstall script (Administrator)
├── build_msi.bat             # MSI build script
└── README.md                 # This file
```

## How It Works

### WinUSB Driver

The `mtkclient.inf` file is a standard Windows INF that instructs the
operating system to bind the built-in `WinUSB.sys` kernel driver to MediaTek
USB devices based on their Vendor ID and Product ID.

When a matching device is plugged in:

1. Windows PnP recognizes the VID/PID from the INF
2. Windows loads `WinUSB.sys` for the device
3. `libusb` (via the `libusb-1.0.dll` bundled with mtkclient) communicates
   with `WinUSB.sys` directly
4. `pyusb` → `libusb` → `WinUSB.sys` → USB device

This eliminates the need for UsbDk, which was previously required as a
third-party kernel filter driver.

### MSI Installer

The MSI package is built using WiX Toolset v3.  It:

1. Copies the driver files to `C:\Program Files\mtkclient\driver\`
2. Uses `pnputil.exe` to install the driver into the Windows driver store
3. Supports clean uninstallation via Add/Remove Programs

### Compatibility

- **Windows 10** version 1507 (build 10240) or later
- **Windows 11** all versions
- **Architecture**: x64 only
- **Test mode**: If driver signing is not available, enable test signing:
  ```
  bcdedit /set testsigning on
  ```

## License

GPLv3 — same as the mtkclient project.
