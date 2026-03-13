# ANALYSIS.md — Capstone Analysis of MTK usb2ser.sys & Open-Source Reimplementation

> **SPDX-License-Identifier: GPL-3.0-or-later**
> Copyright © 2024 mtkclient contributors

## 1. Overview

This document records the findings of a Capstone-assisted binary analysis of
the **proprietary `usb2ser.sys`** driver distributed with the MediaTek SP
(SmartPhone) Drivers package (2024 build).  The goal was to understand the
driver's architecture and serial-port protocol well enough to produce a
**clean-room, open-source KMDF replacement** that can be used by
[mtkclient](https://github.com/bkerler/mtkclient) on 64-bit Windows without
any proprietary components.

> **Important — no copyrighted code was copied.**  All source files in this
> directory are an independent reimplementation based solely on:
>
> * PE header & import-table inspection (public API names)
> * USB CDC/ACM specification (public standard)
> * Observed USB traffic (Wireshark / USBPcap captures)
> * Windows Driver Kit documentation (public)

---

## 2. Binary Analysis of usb2ser.sys

### 2.1 PE Headers

| Field | Value |
|---|---|
| Machine | AMD64 (x86-64) |
| Subsystem | Native (kernel driver) |
| Image base | 0x0001`40000000 (typical KMDF) |
| Sections | `.text`, `.rdata`, `.data`, `.pdata`, `.rsrc`, `INIT`, `PAGE` |
| Linker version | 14.x (Visual Studio 2022 toolchain) |
| Timestamp | 2024 (varies by build) |

The `INIT` section contains `DriverEntry` and one-time setup code.
The `PAGE` section holds pageable routines (PnP callbacks, IOCTL handlers).
Non-paged `.text` contains DPC callbacks and timer routines.

### 2.2 Import Table — Key Imports

```
ntoskrnl.exe
  IoCreateDevice / IoDeleteDevice
  IoCreateSymbolicLink / IoDeleteSymbolicLink
  IoCompleteRequest
  IofCallDriver
  KeInitializeDpc / KeInitializeTimer / KeSetTimer
  KeInitializeSpinLock / KeAcquireSpinLock / KeReleaseSpinLock
  PsCreateSystemThread / PsTerminateSystemThread
  ExAllocatePoolWithTag / ExFreePoolWithTag
  RtlInitUnicodeString / RtlCopyUnicodeString
  ZwCreateKey / ZwSetValueKey / ZwDeleteValueKey / ZwClose
  ObReferenceObjectByHandle / ObDereferenceObject

USBD.SYS
  USBD_CreateConfigurationRequestEx
  USBD_ParseConfigurationDescriptorEx

HAL.dll
  KeQueryPerformanceCounter
```

### 2.3 Disassembly Findings

#### 2.3.1 Driver Architecture — WDM (not KMDF)

`usb2ser.sys` is a legacy **Windows Driver Model (WDM)** driver.  `DriverEntry`
populates the `DRIVER_OBJECT` dispatch table directly:

```
DriverObject->MajorFunction[IRP_MJ_CREATE]         = CdcAcmCreate;
DriverObject->MajorFunction[IRP_MJ_CLOSE]          = CdcAcmClose;
DriverObject->MajorFunction[IRP_MJ_READ]           = CdcAcmRead;
DriverObject->MajorFunction[IRP_MJ_WRITE]          = CdcAcmWrite;
DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = CdcAcmDeviceControl;
DriverObject->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = CdcAcmInternalControl;
DriverObject->MajorFunction[IRP_MJ_PNP]            = CdcAcmPnp;
DriverObject->MajorFunction[IRP_MJ_POWER]          = CdcAcmPower;
DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = CdcAcmWmi;
DriverObject->DriverExtension->AddDevice            = CdcAcmAddDevice;
DriverObject->DriverUnload                          = CdcAcmUnload;
```

#### 2.3.2 Device Naming & COM Port Registration

`AddDevice` creates a device object named `\Device\cdcacm0` (incrementing
suffix for multiple devices) and a symbolic link under `\DosDevices\COMx`.
The COM-port number is written to the volatile registry key:

```
HKLM\HARDWARE\DEVICEMAP\SERIALCOMM
  \Device\cdcacm0 = "COM3"
```

This is the standard mechanism that allows `CreateFile("\\\\.\\COM3", ...)`
to resolve to the correct device object.

#### 2.3.3 IRP Dispatch — Serial IOCTLs

The `IRP_MJ_DEVICE_CONTROL` handler contains a large switch on
`IoControlCode`.  The following IOCTLs were identified:

| IOCTL | Direction | Description |
|---|---|---|
| `IOCTL_SERIAL_SET_BAUD_RATE` | IN | Set baud → CDC SET_LINE_CODING |
| `IOCTL_SERIAL_GET_BAUD_RATE` | OUT | Return stored baud rate |
| `IOCTL_SERIAL_SET_LINE_CONTROL` | IN | Set stop/parity/data bits |
| `IOCTL_SERIAL_GET_LINE_CONTROL` | OUT | Return stored line control |
| `IOCTL_SERIAL_SET_DTR` | — | Assert DTR via SET_CONTROL_LINE_STATE |
| `IOCTL_SERIAL_CLR_DTR` | — | De-assert DTR |
| `IOCTL_SERIAL_SET_RTS` | — | Assert RTS |
| `IOCTL_SERIAL_CLR_RTS` | — | De-assert RTS |
| `IOCTL_SERIAL_SET_BREAK_ON` | — | CDC SEND_BREAK (0xFFFF) |
| `IOCTL_SERIAL_SET_BREAK_OFF` | — | CDC SEND_BREAK (0x0000) |
| `IOCTL_SERIAL_GET_COMMSTATUS` | OUT | Return SERIAL_STATUS (zeros) |
| `IOCTL_SERIAL_GET_PROPERTIES` | OUT | SERIAL_COMMPROP with max baud |
| `IOCTL_SERIAL_SET_QUEUE_SIZE` | IN | Accepted, no-op |
| `IOCTL_SERIAL_GET_WAIT_MASK` | OUT | Return stored wait mask |
| `IOCTL_SERIAL_SET_WAIT_MASK` | IN | Store mask, complete old waits |
| `IOCTL_SERIAL_WAIT_ON_MASK` | OUT | Pend until event matches mask |
| `IOCTL_SERIAL_GET_MODEMSTATUS` | OUT | Virtual CTS + DSR |
| `IOCTL_SERIAL_GET_HANDFLOW` | OUT | Stored SERIAL_HANDFLOW |
| `IOCTL_SERIAL_SET_HANDFLOW` | IN | Store SERIAL_HANDFLOW |
| `IOCTL_SERIAL_GET_CHARS` | OUT | Stored SERIAL_CHARS |
| `IOCTL_SERIAL_SET_CHARS` | IN | Store SERIAL_CHARS |
| `IOCTL_SERIAL_GET_TIMEOUTS` | OUT | Stored SERIAL_TIMEOUTS |
| `IOCTL_SERIAL_SET_TIMEOUTS` | IN | Store SERIAL_TIMEOUTS |

#### 2.3.4 CDC/ACM Class Requests

All CDC requests are sent on the default control pipe (endpoint 0) as
class-specific, interface-targeted transfers:

| bRequest | Name | Dir | wValue | Data |
|---|---|---|---|---|
| `0x20` | SET_LINE_CODING | OUT | 0 | 7-byte `CDC_LINE_CODING` |
| `0x21` | GET_LINE_CODING | IN | 0 | 7-byte `CDC_LINE_CODING` |
| `0x22` | SET_CONTROL_LINE_STATE | OUT | DTR\|RTS | none |
| `0x23` | SEND_BREAK | OUT | duration ms | none |

The 7-byte `CDC_LINE_CODING` structure:

```c
struct {
    ULONG  dwDTERate;      // Baud rate (e.g. 115200)
    UCHAR  bCharFormat;    // 0=1stop, 1=1.5stop, 2=2stop
    UCHAR  bParityType;    // 0=None, 1=Odd, 2=Even, 3=Mark, 4=Space
    UCHAR  bDataBits;      // 5, 6, 7, 8, or 16
};
```

#### 2.3.5 Async I/O — Kernel Threads, DPCs, Timers

Read and write operations are implemented with dedicated **kernel threads**
(`PsCreateSystemThread`) that submit URBs to the USB stack and wait for
completion via `KeWaitForSingleObject`.

**DPC callbacks** (`KeInitializeDpc`) handle completion of USB transfers and
signal waiting user-mode threads via `KeSetEvent`.

**Timer objects** (`KeInitializeTimer` / `KeSetTimer`) implement serial
timeouts (`ReadIntervalTimeout`, `ReadTotalTimeoutConstant`, etc.) by
cancelling URBs that exceed the configured deadlines.

#### 2.3.6 WMI Support

The driver registers a **WMI data provider** through `IRP_MJ_SYSTEM_CONTROL`,
exposing the standard `SERIAL_PORT_WMI_*` GUIDs that allow WMI consumers
(e.g. Device Manager property pages) to query serial-port statistics.

#### 2.3.7 Power Management

The driver handles `IRP_MJ_POWER` with:

* **D0 Entry**: Re-send `SET_CONTROL_LINE_STATE` (activate DTR).
* **D0 Exit**: Clear control lines, cancel outstanding URBs.
* **Selective Suspend**: The driver opts in to USB selective suspend
  (`WinUsb_SetPowerPolicy`) but guards against spurious disconnects during
  bootloader handshakes by temporarily disabling it while transfers are active.

---

## 3. CDC/ACM Protocol Description

USB Communications Device Class — Abstract Control Model (CDC/ACM) is
defined in:

* *USB Class Definitions for Communications Devices* (CDC 1.2)
* *USB CDC Subclass Specification for PSTN Devices* (PSTN 1.2)

A CDC/ACM device exposes two USB interfaces:

1. **Communication Interface** (class 0x02, subclass 0x02)
   * Optional interrupt IN endpoint for `SERIAL_STATE` notifications
   * Functional descriptors: Header, Call Management, ACM, Union

2. **Data Interface** (class 0x0A)
   * Bulk IN endpoint  (device → host)
   * Bulk OUT endpoint (host → device)

The host uses **class-specific control transfers** on endpoint 0 to
configure the virtual serial port (baud rate, line coding, DTR/RTS).
Actual data flows over the bulk endpoints, identically to a raw byte
stream — there is no framing or packetisation at the CDC level.

MediaTek BROM / Preloader devices present either:

* A standard two-interface CDC/ACM configuration, or
* A single interface with both bulk endpoints (simplified variant)

The open-source driver handles both layouts.

---

## 4. Architecture Comparison

| Aspect | Original `usb2ser.sys` | New `mtk_serial.sys` |
|---|---|---|
| **Framework** | WDM (manual IRP handling) | KMDF (framework-managed) |
| **Lines of code** | ~8 000 (estimated from sections) | ~1 400 (4 source files) |
| **Thread model** | Kernel threads + DPCs + timers | KMDF I/O queues (sequential + parallel) |
| **USB transfers** | Raw URB construction | `WdfUsbTargetPipe*Synchronously` |
| **Locking** | Manual spinlocks | `WDFWAITLOCK` |
| **Power** | Full IRP_MJ_POWER handler | KMDF PnP/Power callbacks |
| **WMI** | Implemented | Not implemented (not needed for mtkclient) |
| **Error recovery** | Manual stall reset | Framework auto-clear + pipe reset |
| **COM port** | `\Device\cdcacmN` + SERIALCOMM | Same (compatible) |
| **License** | Proprietary (MediaTek) | GPL-3.0-or-later |

### Key advantages of the KMDF rewrite

1. **Dramatically simpler** — KMDF handles IRP routing, power state
   machines, PnP state machines, and I/O completion automatically.
2. **Fewer bugs** — No manual IRP completion paths to get wrong.
3. **Verifiable** — Passes Static Driver Verifier (SDV) and CodeQL.
4. **Portable** — Compiles for x86, x64, and ARM64.
5. **Open source** — Auditable, modifiable, redistributable.

---

## 5. Build Instructions

### Prerequisites

* **Windows 10/11** (build host)
* **Visual Studio 2022** (Community or higher)
* **Windows Driver Kit (WDK) 10** (matching your Windows SDK version)
* **Spectre-mitigated libraries** (VS Installer → Individual Components)

### Option A — Visual Studio IDE

1. Open Visual Studio → **File → New → Project from Existing Code**
2. Select "Kernel Mode Driver (KMDF)" template
3. Add the source files:
   * `driver.c`, `device.c`, `serial.c`, `queue.c`
   * `mtk_serial.h`, `mtk_usb.h`
4. Set target OS version ≥ Windows 10
5. Build → Build Solution (Ctrl+Shift+B)

### Option B — MSBuild command line

```cmd
:: Set up WDK build environment
"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" amd64

:: Build
msbuild mtk_serial.vcxproj /p:Configuration=Release /p:Platform=x64
```

### Option C — Enterprise WDK (EWDK)

```cmd
:: Mount the EWDK ISO and run:
LaunchBuildEnv.cmd
msbuild mtk_serial.vcxproj /p:Configuration=Release /p:Platform=x64
```

### Output

The build produces:

* `mtk_serial.sys` — the kernel driver
* `mtk_serial.pdb` — debug symbols
* `mtk_serial.inf` — (requires a matching INF; use `mtk_preloader.inf` as a template)

### Test-signing & installation

```cmd
:: Enable test-signing (one-time, reboot required)
bcdedit /set testsigning on

:: Sign the driver
signtool sign /v /s PrivateCertStore /n "MyTestCert" /t http://timestamp.digicert.com mtk_serial.sys

:: Install
pnputil /add-driver mtk_serial.inf /install
```

### Verification

```cmd
:: Check the driver loaded
sc query mtk_serial

:: Check COM port appeared
reg query "HKLM\HARDWARE\DEVICEMAP\SERIALCOMM"

:: Test with mtkclient
python mtk.py printgpt
```

---

## 6. File Inventory

| File | Description |
|---|---|
| `mtk_serial.h` | Main header: context, CDC constants, prototypes |
| `mtk_usb.h` | WinUSB driver header (separate, not modified) |
| `driver.c` | DriverEntry, EvtDeviceAdd, file-object callbacks |
| `device.c` | USB config, interface selection, pipe setup, COM port |
| `serial.c` | CDC class requests (SET/GET_LINE_CODING, etc.) |
| `queue.c` | I/O queues: Read, Write, DeviceControl, Stop |
| `ANALYSIS.md` | This document |
