# mtkclient Windows Driver

Open-source KMDF WinUSB function driver for MediaTek USB devices.

## Architecture

```
User Mode (mtkclient)
├── mtk_usb_driver.dll  ←  WinUSB API (WinUsb_ReadPipe/WritePipe)
├── win32_utils.py      ←  ctypes WinUSB wrapper
└── usblib.py           ←  USB backend (WinUSB → libusb fallback)

Kernel Mode
├── WinUSB.sys          ←  Microsoft inbox WinUSB function driver
├── mtkclient_driver.sys (optional KMDF driver)
└── usbser.sys          ←  Microsoft inbox USB serial driver (fallback)
```

## Driver INF Files

### `mtkclient_winusb.inf` (Recommended)
- **WinUSB driver** — enables direct USB bulk I/O from user-space
- No UsbDk or libusb required
- Windows 11 DCH compliant (`PnpLockdown=1`, `DefaultDestDir=13`)
- Comprehensive VID/PID list from MTK SP Drivers
- `.NTamd64` and `.NTamd64.10.0...17763` decorated sections
- `[DDInstall.Wdf]` section for KMDF 1.11

### `../../mtkclient/Windows/driver/mtkclient_preloader.inf` (Fallback)
- **usbser.sys driver** — device appears as COM port
- Windows 11 DCH compliant
- Use when WinUSB is not needed

## Install

### Automatic (NSIS/MSI installer)
Both drivers are installed automatically during setup.

### Manual
```cmd
# As Administrator:

# Install WinUSB driver (recommended)
pnputil /add-driver mtkclient_winusb.inf /install

# Install serial driver (fallback)  
pnputil /add-driver mtkclient_preloader.inf /install

# Or use the install script:
install_driver.bat /winusb
```

## Build KMDF Driver (Optional)

The KMDF driver (`mtkclient_driver.sys`) is optional. The INF-only approach
with WinUSB.sys is sufficient for most use cases.

### Prerequisites
- Visual Studio 2022
- Windows Driver Kit (WDK) 10.0.22621.0+
- Spectre-mitigated libraries

### Build
```cmd
msbuild mtkclient_driver.vcxproj /p:Configuration=Release /p:Platform=x64
```

### Output
- `x64\Release\mtkclient_driver.sys` — kernel driver
- `x64\Release\mtkclient_driver.pdb` — debug symbols

## Source Code

| File | Description |
|------|-------------|
| `src/driver.c` | KMDF DriverEntry, device add callback |
| `src/device.c` | USB device init, endpoint enumeration, power management |
| `src/driver.h` | Shared header, context structures, GUID definitions |
| `mtkclient_driver.vcxproj` | Visual Studio WDK project |
| `mtkclient_winusb.inf` | WinUSB driver INF (Windows 11 DCH) |

## Supported Devices

| VID | PID | Mode | Description |
|-----|-----|------|-------------|
| 0x0E8D | 0x0003 | BROM | MediaTek Bootrom |
| 0x0E8D | 0x2000 | Preloader | MediaTek Preloader |
| 0x0E8D | 0x2001 | DA | MediaTek DA Agent |
| 0x0E8D | 0x2006 | VCOM | MediaTek VCOM |
| 0x0E8D | 0x2007 | Meta | MediaTek Meta Mode |
| 0x0E8D | 0x2024 | Fastboot | MediaTek Bootloader |
| 0x0E8D | 0x20FF | Preloader | MediaTek Preloader (alt) |
| 0x0E8D | 0x3000 | Preloader | MediaTek Preloader (alt) |
| 0x0E8D | 0x6000 | Preloader | MediaTek Preloader (alt) |
| 0x0E8D | 0x0023/43/63/A5 | COM | MediaTek COM Ports |
| 0x1004 | 0x6000 | Preloader | LG MTK Preloader |
| 0x22D9 | 0x0006 | Preloader | OPPO MTK Preloader |
| 0x0FCE | Various | BROM | Sony MTK Bootrom |

## License

GPLv3 — see repository root LICENSE file.
