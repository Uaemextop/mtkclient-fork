# Google USB Driver r13 — Analysis & Comparison with MediaTek SP Drivers

## Source

Google USB Driver r13 downloaded from the mirror at
`https://github.com/Uaemextop/mtkclient-fork/releases/download/v11/usb_driver_r13-windows.zip`
(original URL: `https://dl.google.com/android/repository/usb_driver_r13-windows.zip`).

The `android_winusb.inf` and all original binaries (catalogs, CoInstaller DLLs)
in this directory are the unmodified Google r13 driver files.

## Files in the Google r13 package

| File | Description |
|------|-------------|
| `android_winusb.inf` | Driver INF — WinUSB for ADB/Bootloader |
| `androidwinusb86.cat` | Signed catalog for x86 |
| `androidwinusba64.cat` | Signed catalog for x64 |
| `amd64/WdfCoInstaller01009.dll` | KMDF CoInstaller (legacy, not needed Win10+) |
| `amd64/winusbcoinstaller2.dll` | WinUSB CoInstaller (legacy, not needed Win10+) |
| `amd64/WUDFUpdate_01009.dll` | UMDF update DLL |
| `i386/WdfCoInstaller01009.dll` | KMDF CoInstaller x86 |
| `i386/winusbcoinstaller2.dll` | WinUSB CoInstaller x86 |
| `i386/WUDFUpdate_01009.dll` | UMDF update DLL x86 |
| `source.properties` | `Pkg.Revision=13` |

## Detailed Comparison: MTK original vs Google r13

The MTK SP Drivers 20160804 `android_winusb.inf` (at `../original/Android/`)
is a **direct copy** of Google's `android_winusb.inf` with only the device IDs
and provider name changed.

### Identical between MTK and Google

| Element | Value |
|---------|-------|
| Class | `AndroidUsbDeviceClass` |
| ClassGuid | `{3F966BD9-FA04-4ec5-991C-D326973B5128}` |
| DriverVer | `08/28/2014,11.0.0000.00000` |
| Catalog names | `androidwinusb86.cat` / `androidwinusba64.cat` |
| ClassInstall32 | Identical `AndroidWinUsbClassReg` section |
| DeviceInterfaceGUIDs | `{F72FE0D4-CBCB-407d-8814-9ED673D0DD6B}` (ADB standard) |
| USB_Install section | `Include=winusb.inf`, `Needs=WINUSB.NT` |
| USB_Install.Wdf | `KmdfService=WINUSB`, `KmdfLibraryVersion=1.9` |
| CoInstallers | `WdfCoInstaller01009.dll` + `WinUSBCoInstaller2.dll` |

### Differences

| Element | Google r13 | MTK SP Drivers |
|---------|-----------|----------------|
| Provider | `Google, Inc.` | `MediaTek` |
| ClassName string | `Android Device` | `Android Phone` |
| Manufacturer section | `[Google.NTx86/NTamd64]` | `[MediaTek.NTx86/NTamd64]` |
| VID | `0x18D1` (Google) | `0x0E8D` (MediaTek) |
| Device types | ADB + Bootloader (Nexus/Pixel) | ADB + Bootloader (MTK SoC) |
| ADB entries | ~35 (Nexus/Pixel/Glass/Tango) | 45 (standard + C2K + MIDI + MBIM) |

### MTK Device IDs from original android_winusb.inf

**45 total entries** (VID `0x0E8D`):

- **1 SingleAdbInterface**: `PID_201C` (whole device)
- **1 SingleBootLoaderInterface**: `PID_2024` (whole device)
- **28 standard CompositeAdbInterface**: PIDs `2003`-`2062` with various `MI_xx`
- **10 C2K related**: PIDs `2032`, `2034`, `2035`, `2037`, `2039`, `2050`, `2053`, `2054`, `2063`, `2064`
- **2 MIDI related**: PIDs `2048`, `2049`
- **7 MBIM related**: PIDs `2056`, `2057`, `2059`, `205A`, `205B`, `205C`, `205F`

### MTK MTP Device IDs from original wpdmtp.inf

**14 entries** (VID `0x0E8D`):

- `PID_2008` (whole device)
- `PID_200A&MI_00`, `PID_2012&MI_00`, `PID_2016&MI_00`, `PID_2017&MI_00`
- `PID_2018&MI_00`, `PID_2019&MI_00`, `PID_201C&MI_00`, `PID_201D&MI_00`
- `PID_2021&MI_00`, `PID_2022&MI_00`, `PID_2026&MI_00`, `PID_202A&MI_00`

## What We Modified (mtkclient_adb.inf)

The `../mtkclient_adb.inf` is derived from Google's `android_winusb.inf` r13
with these changes:

### Removed (legacy, not needed on Windows 10+)
- `[USB_Install.CoInstallers]` section
- `[CoInstallers_AddReg]` and `[CoInstallers_CopyFiles]`
- `[SourceDisksNames]` and `[SourceDisksFiles]`
- `[USB_Install.Wdf]` section (inbox KMDF handles this on Win10+)
- `CatalogFile.NTx86` (x64-only)
- `[*.NTx86]` sections (x64-only)

### Added (DCH compliance + MTK devices)
- `PnpLockdown=1` for DCH compliance
- `DefaultDestDir=13` (Driver Store, DCH compliant)
- `CatalogFile=mtkclient_adb.cat` (single x64 catalog)
- All 45 MTK ADB/Bootloader entries from original `android_winusb.inf`
- All 14 MTK MTP entries from original `wpdmtp.inf`
- MTK BROM (`PID_0003`), Preloader (`PID_2000/20FF/3000/6000`), DA (`PID_2001`)
- Sony BROM devices (VID `0x0FCE`)
- LG Preloader (VID `0x1004`)
- OPPO Preloader (VID `0x22D9`)
- Selective suspend disable + security ACL
- Both ADB GUID (`{F72FE0D4-...}`) and mtkclient GUID (`{1D0C3B4F-...}`)

### Kept (compatibility)
- `AndroidUsbDeviceClass` and ClassGuid `{3F966BD9-...}` — for Device Manager
- `DeviceInterfaceGUIDs={F72FE0D4-...}` — for `adb.exe` compatibility
- All original Google Nexus/Pixel/Glass/Tango device entries preserved
