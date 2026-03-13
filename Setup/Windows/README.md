# mtkclient WinUSB Driver for Windows

## Overview

This directory contains a complete **open-source KMDF USB driver** for
MediaTek bootrom and preloader USB devices.  It provides native Windows 10/11
x64 support, eliminating the need for the third-party **UsbDk** driver.

The driver consists of two components:

1. **WinUSB INF driver** — A standard Windows INF that binds Microsoft's
   built-in `WinUSB.sys` to MediaTek devices.  This is the easiest option
   and works for all common mtkclient operations.

2. **Full KMDF driver source** (in `driver/src/`) — A complete Windows
   Kernel-Mode Driver Framework (KMDF) driver written in C.  It implements
   CDC ACM class requests (SET_LINE_CODING, GET_LINE_CODING, SEND_BREAK,
   SET_CONTROL_LINE_STATE) plus raw vendor/class control transfers required
   by the kamakiri bootrom exploits.

## Supported Devices

VID/PID values extracted from the official **MediaTek SP Drivers 20160804**
(`cdc-acm.inf`, `android_winusb.inf`) and extended with mtkclient IDs.

### Core Modes (single-interface)

| VID      | PID      | Mode                     |
|----------|----------|--------------------------|
| `0x0E8D` | `0x0003` | Bootrom (BROM)           |
| `0x0E8D` | `0x2000` | Preloader                |
| `0x0E8D` | `0x2001` | Download Agent (DA)      |
| `0x0E8D` | `0x2007` | Meta Mode / Generic VCOM |
| `0x0E8D` | `0x20FF` | Preloader (mtkclient)    |
| `0x0E8D` | `0x3000` | Preloader (mtkclient)    |
| `0x0E8D` | `0x6000` | Preloader (mtkclient)    |
| `0x0E8D` | `0x2024` | Android Bootloader       |

### Composite Devices (multi-interface)

Over 60 composite VCOM and ADB interface entries from the SP drivers are
included.  See `driver/mtkclient.inf` for the complete list.

### Other Vendors

| VID      | PID      | Description        |
|----------|----------|--------------------|
| `0x1004` | `0x6000` | LG Preloader       |
| `0x22D9` | `0x0006` | OPPO Preloader     |

## Quick Install (WinUSB INF)

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

## Building the KMDF Driver from Source

### Prerequisites

- **Visual Studio 2022** with C++ Desktop development workload
- **Windows Driver Kit (WDK)** for Windows 11
- **Windows 11 SDK**

### Build Steps

1. Open VS Developer Command Prompt (x64):
   ```
   msbuild driver\mtkclient_driver.vcxproj /p:Configuration=Release /p:Platform=x64
   ```

2. Or open `driver\mtkclient_driver.vcxproj` in Visual Studio and build.

3. The built `mtkclient.sys` driver will be in `Release\x64\`.

### Signing (for production)

Generate a test certificate and sign the driver:
```
makecert -r -pe -ss PrivateCertStore -n "CN=mtkclient" mtkclient.cer
signtool sign /s PrivateCertStore /n "mtkclient" /t http://timestamp.digicert.com driver\mtkclient.sys
inf2cat /driver:driver /os:10_X64
signtool sign /s PrivateCertStore /n "mtkclient" /t http://timestamp.digicert.com driver\mtkclient.cat
```

For development, enable test signing: `bcdedit /set testsigning on`

## MSI Installer

Build a distributable MSI package:

```
build_msi.bat
```

Output: `output\mtkclient_driver.msi`

See `installer\mtkclient_driver.wxs` for the WiX source.

## Driver Source Architecture

```
driver/
├── mtkclient.inf              # WinUSB INF (all VID/PIDs)
├── mtkclient_driver.vcxproj   # VS/WDK project file
└── src/
    ├── public.h               # Shared header (GUID, IOCTLs, structures)
    ├── driver.h               # Internal driver header
    ├── driver.c               # DriverEntry, EvtDeviceAdd
    ├── device.c               # USB device lifecycle, pipe configuration
    ├── queue.c                # I/O queue: Read, Write, DeviceIoControl
    └── mtk_usb.c              # MTK-specific: CDC ACM, exploits, bulk I/O
```

### Key Components

- **driver.c** — KMDF driver entry.  Creates the WDFDEVICE, registers PnP
  callbacks, and initializes the I/O queue.

- **device.c** — USB target creation, configuration selection, pipe
  discovery.  Supports both single-interface (BROM) and multi-interface
  (preloader CDC ACM) devices.

- **queue.c** — Default parallel I/O queue.  `ReadFile` → bulk IN,
  `WriteFile` → bulk OUT, `DeviceIoControl` → IOCTLs.

- **mtk_usb.c** — MediaTek protocol implementation:
  - CDC ACM: `SET_LINE_CODING`, `GET_LINE_CODING`, `SET_CONTROL_LINE_STATE`,
    `SEND_BREAK`
  - Arbitrary control transfers for kamakiri exploits
  - USB port reset and re-enumeration
  - Bulk read/write for BROM handshake and DA data transfers

### Custom IOCTLs

| IOCTL                              | Purpose                          |
|------------------------------------|----------------------------------|
| `IOCTL_MTKCLIENT_GET_DEVICE_INFO`  | Get VID/PID/version/status       |
| `IOCTL_MTKCLIENT_SET_LINE_CODING`  | CDC ACM baud/parity/stop/data    |
| `IOCTL_MTKCLIENT_GET_LINE_CODING`  | Read current line coding         |
| `IOCTL_MTKCLIENT_SET_CONTROL_LINE_STATE` | Set DTR/RTS              |
| `IOCTL_MTKCLIENT_SEND_BREAK`       | Send break signal                |
| `IOCTL_MTKCLIENT_VENDOR_CTRL_TRANSFER` | Raw USB control transfer    |
| `IOCTL_MTKCLIENT_RESET_DEVICE`     | USB port reset                   |
| `IOCTL_MTKCLIENT_REENUMERATE`      | Force PnP re-enumeration         |
| `IOCTL_MTKCLIENT_GET_PIPE_INFO`    | Get endpoint configuration       |

## Compatibility

- **Windows 10** version 1507 (build 10240) or later
- **Windows 11** all versions
- **Architecture**: x64 only

## Test Signing

The CI workflow builds the driver with a self-signed test certificate.
Windows requires **test signing mode** to load drivers signed with test
certificates.

### Automatic (MSI installer or install_driver.bat)

Both the MSI installer and `install_driver.bat` will automatically:
1. Install the test certificate to Trusted Root + Trusted Publisher stores
2. Enable test signing mode (`bcdedit /set testsigning on`)
3. Stage the driver via `pnputil /add-driver`

**A reboot is required** after the first install for test signing to activate.

### Manual

```powershell
# As Administrator:
certutil -addstore Root output\mtkclient_test_cert.cer
certutil -addstore TrustedPublisher output\mtkclient_test_cert.cer
bcdedit /set testsigning on
# Reboot, then:
pnputil /add-driver driver\mtkclient.inf /install
```

### Troubleshooting Code 43

If Device Manager shows **Code 43** ("Windows stopped this device"):
1. Verify test signing mode is active: `bcdedit | findstr testsigning`
2. Verify the certificate is installed: `certutil -store TrustedPublisher`
3. If Secure Boot is enabled, you may need to disable it in BIOS first
4. Reboot after making any changes
5. Unplug and replug the USB cable

### Note on Secure Boot

Enabling test signing mode (`bcdedit /set testsigning on`) may conflict
with Secure Boot. If your system has Secure Boot enabled:
- Enter BIOS/UEFI setup and disable Secure Boot, **or**
- Use a production EV code signing certificate (not included)

## License

GPLv3 — same as the mtkclient project.
