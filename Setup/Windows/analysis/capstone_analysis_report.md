# Capstone Binary Analysis Report — MTK SP Drivers

**Date:** 2026-03-13
**Tool:** Capstone Engine 5.0.7 (Python bindings)
**Source:** `MTK_Driver_Auto_Installer_SP_Drivers_20160804.zip` from mtkclient-fork v11 release

---

## 1. Files Analyzed

| File | Arch | Type | Size | Timestamp |
|------|------|------|------|-----------|
| `CDC/cdc-acm.inf` | — | INF | 16,116 B | 2016-07-15 |
| `CDC/cdc-acm.cat` | — | PKCS#7 Signed Catalog | 19,221 B | 2016-07-16 |
| `CDC/x64/usb2ser.sys` | AMD64 | PE32+ native kernel driver | 151,184 B | 2016-07-16 |
| `CDC/x86/usb2ser.sys` | i386 | PE32 native kernel driver | 119,952 B | 2016-07-16 |
| `mbim/mtkmbim7_x64.sys` | AMD64 | PE32+ NDIS 6.20 miniport | 282,368 B | 2016-03-17 |
| `mbim/mtkmbim7_x64.inf` | — | INF | 3,791 B | 2016-03-17 |
| `mbim/mtkmbim7_x64.cat` | — | PKCS#7 Signed Catalog | 10,523 B | 2016-03-17 |
| `mbim/mtkmbimv_x64.sys` | AMD64 | PE32+ NDIS 6.0 miniport | 295,168 B | 2016-03-17 |
| `mbim/mtkmbimv_x64.inf` | — | INF | 3,406 B | 2016-03-17 |
| `mbim/mtkmbimv_x64.cat` | — | PKCS#7 Signed Catalog | 10,524 B | 2016-03-17 |
| `mbim/mtkmbimx_x64.sys` | AMD64 | PE32+ NDIS 5.1 miniport | 295,168 B | 2016-03-17 |
| `mbim/mtkmbimx_x64.inf` | — | INF | 3,336 B | 2016-03-17 |
| `mbim/mtkmbimx_x64.cat` | — | PKCS#7 Signed Catalog | 10,524 B | 2016-03-17 |

---

## 2. CDC Driver Analysis: `usb2ser.sys`

### 2.1 PE Header Summary

| Property | x64 | x86 |
|----------|-----|-----|
| Machine | AMD64 (0x8664) | i386 (0x014C) |
| Entry Point RVA | 0x00014200 | 0x000181BE |
| Image Base | 0x0000000000010000 | 0x00010000 |
| Sections | 8 (.text, .rdata, .data, .pdata, PAGE, INIT, .rsrc, .reloc) | 7 (.text, .rdata, .data, PAGE, INIT, .rsrc, .reloc) |
| Functions identified | 553 (x64) | 246 (x86) |
| Service name | `wdm_usb` | `wdm_usb` |
| Description | "USB Modem/Serial Device Driver" | "USB Modem/Serial Device Driver" |

### 2.2 Imported DLLs

**NTOSKRNL.exe** — Core kernel APIs:
- Memory: `ExAllocatePoolWithTag`, `ExFreePoolWithTag`
- I/O: `IofCompleteRequest`, `IofCallDriver`, `IoCreateDevice`, `IoDeleteDevice`, `IoAttachDeviceToDeviceStack`, `IoDetachDevice`
- IRP: `IoAllocateIrp`, `IoFreeIrp`, `IoReuseIrp`, `IoCancelIrp`
- PnP: `IoRegisterDeviceInterface`, `IoSetDeviceInterfaceState`, `IoGetDeviceProperty`
- Power: `PoStartNextPowerIrp`, `PoRequestPowerIrp`, `PoSetPowerState`, `PoCallDriver`
- Sync: `KeInitializeEvent`, `KeSetEvent`, `KeClearEvent`, `KeWaitForSingleObject`, `KeAcquireSpinLockRaiseToDpc`, `KeReleaseSpinLock`
- Timer: `KeInitializeTimer`, `KeSetTimer`, `KeCancelTimer`, `KeReadStateTimer`
- DPC: `KeInitializeDpc`, `KeInsertQueueDpc`, `KeRemoveQueueDpc`
- Thread: `PsCreateSystemThread`, `PsTerminateSystemThread`, `KeSetPriorityThread`
- Work Items: `IoAllocateWorkItem`, `IoFreeWorkItem`, `IoQueueWorkItem`
- Registry: `IoOpenDeviceRegistryKey`, `ZwQueryValueKey`, `ZwSetValueKey`, `RtlWriteRegistryValue`, `RtlDeleteRegistryValue`
- Remove Lock: `IoInitializeRemoveLockEx`, `IoAcquireRemoveLockEx`, `IoReleaseRemoveLockEx`, `IoReleaseRemoveLockAndWaitEx`
- Symbolic Links: `IoCreateSymbolicLink`, `IoDeleteSymbolicLink`
- WMI: `IoWMIRegistrationControl`, `IoWMIWriteEvent`
- Cancel: `IoAcquireCancelSpinLock`, `IoReleaseCancelSpinLock`
- Memory mapping: `MmMapLockedPagesSpecifyCache`
- Bug Check: `KeBugCheckEx`

**USBD.SYS** — USB Driver stack:
- `USBD_CreateConfigurationRequestEx` — Create USB configuration descriptor
- `USBD_ParseConfigurationDescriptorEx` — Parse USB configuration descriptor

### 2.3 Driver Architecture (Decompiled Function Map)

The driver implements a complete WDM (Windows Driver Model) USB CDC ACM class driver with the following subsystems:

#### 2.3.1 Entry & Dispatch Table
```
DriverEntry                     — Driver initialization, sets MajorFunction table
DispatchFileOp                  — IRP_MJ_CREATE / IRP_MJ_CLOSE
DispatchRead                    — IRP_MJ_READ (serial data input)
DispatchWrite                   — IRP_MJ_WRITE (serial data output)
DispatchIoCtrl                  — IRP_MJ_DEVICE_CONTROL (serial IOCTLs)
DispatchFlush                   — IRP_MJ_FLUSH_BUFFERS
DispatchPnP                     — IRP_MJ_PNP (Plug and Play)
DispatchPower                   — IRP_MJ_POWER
DispatchSysCtrl                 — IRP_MJ_SYSTEM_CONTROL (WMI)
FileOp_Create                   — Handle device open
FileOp_Close                    — Handle device close
```

#### 2.3.2 Plug and Play (PnP) Subsystem
```
PnP_StartDevice                 — Configure and start USB device
PnP_RemoveDevice                — Remove device on unplug
PnP_SurpriseRemoval             — Handle unexpected removal
PnP_QueryRemoveDevice           — Prepare for removal
PnP_CancelRemoveDevice          — Cancel pending removal
PnP_QueryCapabilities           — Report device capabilities
PnP_CreateSymbolicLink          — Create \Device\cdcacmN and COM port link
PnP_RemoveSymbolicLinks         — Remove symbolic links on removal
PnP_SyncPassDownIrp             — Forward PnP IRPs to lower driver
```

#### 2.3.3 Power Management Subsystem
```
PWR_HandleQueryPowerSystem      — Handle system power query
PWR_HandleQueryPowerDevice      — Handle device power query
PWR_HandleSetPowerSystem        — Handle system power state change
PWR_HandleSetPowerDevice        — Handle device power state change
PWR_HandleWaitWake               — Handle wake-from-suspend
PWR_RequestPowerIrpDevice       — Request device power IRP
PWR_ProcessPowerUpDevice        — Power up device sequence
PWR_PowerUpDeviceWorkerRoutine  — Worker thread for power up
PWR_PowerUpDeviceIrpCompletion  — Completion for power up
PWR_PowerDownDeviceIrpCompletion — Completion for power down
PWR_UpdateDeviceCapabilites     — Read device power capabilities
PWR_IssueIdleTimer              — Start selective suspend timer
PWR_CancelIdleTimer             — Cancel selective suspend timer
PWR_IdleDpcRoutine              — DPC for idle timeout
PWR_IdleNotificationCallback    — Callback when device is idle
PWR_IssueIdleRequestIrp         — Issue idle notification to bus driver
PWR_CancelIdleNotificationIrp   — Cancel idle notification
PWR_IssuelWaitWakeIrp           — Issue wait-wake IRP
PWR_CancelWaitWakeIrp           — Cancel wait-wake IRP
PWR_GetIdleInfo                 — Read idle settings from registry
PWR_CheckIdleForDataTrans       — Check if data transfer prevents idle
```

#### 2.3.4 Serial Port Control Subsystem (IOCTL handlers)
```
Ctrl_GetBaudRate                — IOCTL_SERIAL_GET_BAUD_RATE
Ctrl_SetBaudRate                — IOCTL_SERIAL_SET_BAUD_RATE
Ctrl_GetLineControl             — IOCTL_SERIAL_GET_LINE_CONTROL
Ctrl_SetLineControl             — IOCTL_SERIAL_SET_LINE_CONTROL (data bits, stop bits, parity)
Ctrl_GetLineCoding              — USB CDC GET_LINE_CODING request
Ctrl_SetLineCoding              — USB CDC SET_LINE_CODING request
Ctrl_GetHandflow                — IOCTL_SERIAL_GET_HANDFLOW
Ctrl_SetHandflow                — IOCTL_SERIAL_SET_HANDFLOW (hardware flow control)
Ctrl_GetModemStatus             — IOCTL_SERIAL_GET_MODEMSTATUS (DSR, CTS, RI, DCD)
Ctrl_SetDtr                     — IOCTL_SERIAL_SET_DTR
Ctrl_ClrDtr                     — IOCTL_SERIAL_CLR_DTR
Ctrl_SetRts                     — IOCTL_SERIAL_SET_RTS
Ctrl_ClrRts                     — IOCTL_SERIAL_CLR_RTS
Ctrl_GetDtrRts                  — IOCTL_SERIAL_GET_DTRRTS
Ctrl_SetBreakOn                 — IOCTL_SERIAL_SET_BREAK_ON
Ctrl_SetBreakOff                — IOCTL_SERIAL_SET_BREAK_OFF
Ctrl_GetTimeouts                — IOCTL_SERIAL_GET_TIMEOUTS
Ctrl_SetTimeouts                — IOCTL_SERIAL_SET_TIMEOUTS
Ctrl_GetChars                   — IOCTL_SERIAL_GET_CHARS
Ctrl_SetChars                   — IOCTL_SERIAL_SET_CHARS
Ctrl_SetQueueSize               — IOCTL_SERIAL_SET_QUEUE_SIZE
Ctrl_GetWaitMask                — IOCTL_SERIAL_GET_WAIT_MASK
Ctrl_SetWaitMask                — IOCTL_SERIAL_SET_WAIT_MASK
Ctrl_WaitOnMask                 — IOCTL_SERIAL_WAIT_ON_MASK
Ctrl_Purge                      — IOCTL_SERIAL_PURGE
Ctrl_GetCommStatus              — IOCTL_SERIAL_GET_COMMSTATUS
Ctrl_GetProperties              — IOCTL_SERIAL_GET_PROPERTIES
Ctrl_GetStats                   — IOCTL_SERIAL_GET_STATS
Ctrl_ClearStats                 — IOCTL_SERIAL_CLEAR_STATS
Ctrl_ConfigSize                 — IOCTL_SERIAL_CONFIG_SIZE
Ctrl_SetDefaultLineState        — Set initial DTR/RTS state
Ctrl_UpdateSerialState          — Process serial state notification from device
Ctrl_LsrMstInsert               — Line status / modem status insertion
Ctrl_InitWaitMaskThread         — Initialize wait mask thread
Ctrl_WaitMaskThread             — Thread for WAIT_ON_MASK completion
Ctrl_CompletePendingWaitOnMask  — Complete pending wait operations
Ctrl_CancelWaitMaskThread       — Cancel wait mask thread
Ctrl_WaitOnMaskCancelRoutine    — IRP cancel routine for wait mask
Ctrl_WaitMaskComplete           — Wait mask completion callback
```

#### 2.3.5 Data Transfer Subsystem (USB Bulk Pipes)
```
Data_Init                       — Initialize data transfer structures
Data_DeInit                     — Deinitialize data structures
Data_InitBufferManager          — Initialize read/write buffer manager
Data_DeInitBufferManager        — Cleanup buffer manager
Data_ReserveResources            — Pre-allocate URBs and buffers
Data_StartTransfer              — Start USB bulk IN/OUT transfers
Data_StopTransfer               — Stop all active transfers
Data_CleanupTransfer            — Cleanup transfer resources
Data_StartInTrans               — Submit bulk IN URBs (device → host)
Data_StopInTrans                — Stop bulk IN transfers
Data_StartIntrTrans             — Submit interrupt IN URBs (serial state notifications)
Data_StopIntrTrans              — Stop interrupt transfers
Data_StopOutTrans               — Stop bulk OUT transfers
Data_InComplete                 — Bulk IN URB completion callback
Data_InDpcCallback              — DPC for processing received data
Data_InTransReorder             — Reorder out-of-sequence IN transfers
Data_OutComplete                — Bulk OUT URB completion callback
Data_OutDpcCallback             — DPC for processing write completions
Data_OutSendZLP                 — Send Zero-Length Packet for transfer end
Data_IntrUrbComplete            — Interrupt URB completion (serial state)
Data_QueueInDpc                 — Queue IN processing DPC
Data_QueueInReoderDpc           — Queue reorder DPC
Data_QueueInterruptDpc          — Queue interrupt DPC
Data_QueueOutDpc                — Queue OUT processing DPC
Data_CompleteRead               — Complete pending read IRP with data
Data_CompleteReadRequest        — Complete single read request
Data_CompleteWriteRequest       — Complete single write request
Data_CleanReadIrps              — Cancel all pending read IRPs
Data_CleanWriteIrps             — Cancel all pending write IRPs
Data_CancelRead                 — Cancel routine for read IRPs
Data_CancelWrite                — Cancel routine for write IRPs
Data_CancelInPipe               — Cancel pending IN pipe URBs
Data_CancelOutPipe              — Cancel pending OUT pipe URBs
Data_FillinDataFromBuffer       — Copy data from ring buffer to IRP
Data_FlushWindow                — Flush data window/ring buffer
Data_CleanWindow                — Clean data window
Data_FreeBMBuffer               — Free buffer manager buffer
Data_GetDataConfig              — Get USB data pipe configuration
Data_SerialStateNotify          — Process CDC SERIAL_STATE notification
Data_ResetInPipeCallback        — Reset stalled IN pipe
Data_ResetIntrPipeCallback      — Reset stalled interrupt pipe
Data_ResetOutPipeCallback       — Reset stalled OUT pipe
Data_StartResetInPipe           — Initiate IN pipe reset
Data_StartResetIntrPipe         — Initiate interrupt pipe reset
Data_StartResetOutPipe          — Initiate OUT pipe reset
Data_SubmitResetPipeUrb         — Submit pipe reset URB to USB stack
Data_ReadIntervalTimeout        — Handle inter-character timeout
Data_ReadTotalTimeoutCallback   — Handle total read timeout
Data_StartReadIntervalTimeout   — Start interval timeout timer
Data_StartReadTotalTimeout      — Start total read timeout timer
Data_StopReadTotalTimeout       — Cancel total read timeout
Data_StartWriteTimeout          — Start write timeout timer
Data_StopWriteTimeout           — Cancel write timeout
Data_WriteTotalTimeoutCallback  — Handle write timeout expiry
```

#### 2.3.6 WMI (Windows Management Instrumentation)
```
WMI_PowerQueryWmiDataBlock      — Query WMI power data
WMI_PowerSetWmiDataBlock        — Set WMI power data
WMI_PowerSetWmiDataItem         — Set single WMI data item
WMI_WmiDeregistration           — Deregister WMI provider
```

### 2.4 USB Communication Protocol

The driver communicates with MTK devices using the **USB CDC ACM** (Communications Device Class - Abstract Control Model) protocol:

1. **Control Pipe (EP0):** Used for CDC class-specific requests:
   - `SET_LINE_CODING` (0x20): Set baud rate, data bits, stop bits, parity
   - `GET_LINE_CODING` (0x21): Read current line coding
   - `SET_CONTROL_LINE_STATE` (0x22): Set DTR/RTS signals

2. **Bulk IN Pipe:** Receives serial data from device (device → host)
   - Multiple outstanding URBs for throughput
   - DPC-based completion with reordering support

3. **Bulk OUT Pipe:** Sends serial data to device (host → device)
   - Zero-Length Packet support for transfer boundaries
   - DPC-based completion

4. **Interrupt IN Pipe:** Receives serial state notifications:
   - `SERIAL_STATE` (0xA1): DSR, DCD, RI, break, framing/parity error

### 2.5 Key Registry Settings

| Value | Type | Description |
|-------|------|-------------|
| `IdleEnable` | DWORD | Enable selective suspend (0=disabled) |
| `IdleTime` | DWORD | Idle timeout in seconds (default 10) |
| `IdleWWBinded` | DWORD | Wait-wake bound to idle (1=yes) |
| `MaxInIRPNum` | DWORD | Max outstanding IN URBs |
| `MaxOutIRPNum` | DWORD | Max outstanding OUT URBs |
| `PortName` | STRING | COM port name (COMx) |

### 2.6 Hardware IDs (from cdc-acm.inf)

| Hardware ID | Description |
|-------------|-------------|
| `USB\VID_0E8D&PID_0003` | **MediaTek BootROM / BROM** — Primary flash tool port |
| `USB\VID_0E8D&PID_2000` | **MediaTek PreLoader** — Preloader VCOM |
| `USB\VID_0E8D&PID_2001` | **MediaTek DA (Download Agent)** — Flash download port |
| `USB\VID_0E8D&PID_2006&MI_02` | MediaTek USB VCOM (composite) |
| `USB\VID_0E8D&PID_2007` | MediaTek USB VCOM |
| `USB\VID_0E8D&PID_200A–205F` | Various Meta mode / ETS / ELT / Modem / AT ports |

---

## 3. MBIM Drivers Analysis

### 3.1 mtkmbim7_x64.sys — NDIS 6.20 Miniport

| Property | Value |
|----------|-------|
| Type | Network miniport driver (NDIS 6.20) |
| Target | Windows 7+ (NTamd64.6.1) |
| Service | `mtkmbim` |
| Interface | WWANPP (Mobile Broadband) |
| Source tree | `v:\working\sylvia\trunk\sylvia\mcu\driver\mbim\` |

**Key source files identified from debug strings:**
- `net/ndis6/nal.c` — Network Abstraction Layer
- `net/ndis6/vmp.c` — Virtual Miniport
- `net/ndis6/mp_pnp.c` — Miniport PnP handling
- `net/ndis6/mp_main.c` — Miniport main entry
- `net/ndis6/mp_send.c` — Send path
- `net/ndis6/mp_recv.c` — Receive path
- `hif/usb/wdm/hif_if.c` — Host Interface (USB WDM)
- `hif/usb/wdm/hif_data.c` — Data path
- `hif/usb/wdm/hif_ctrl.c` — Control path

**Key functions from debug strings:**
- `ndis_indicate_connect` — Indicate connection status
- `hif_ctrl_configure_device` — Configure USB device
- `hif_get_mbim_func_desc` — Get MBIM function descriptor
- `hif_start_mbim_ctrl` / `hif_stop_mbim_ctrl` — Start/stop MBIM control channel
- `hif_start_mbim_data` / `hif_stop_mbim_data` — Start/stop MBIM data channel
- `wwan_get_device_service` — Get WWAN device service
- `mp_device_pnp_event` / `vmp_device_pnp_event` — PnP event handlers

**MBIM Hardware IDs:**
| ID | Description |
|----|-------------|
| `USB\VID_0E8D&PID_7106&MI_00` | Primary MBIM interface |
| `USB\VID_0E8D&PID_00A5&MI_00` | MBIM variant (REV 0001/0300) |
| `USB\VID_0E8D&PID_00A6&MI_01` | MBIM variant (REV 0001/0300) |
| `USB\VID_0E8D&PID_00AA&MI_00` | MBIM variant |
| `USB\VID_0E8D&PID_2055&MI_00` | MBIM (shared with CDC VCOM MI_02) |
| `USB\VID_0E8D&PID_2057&MI_00` | MBIM (shared with CDC VCOM MI_03) |

### 3.2 mtkmbimv_x64.sys — NDIS 6.0 Miniport
- Same as mtkmbim7 but targets **Windows Vista** (NTamd64.6.0)
- Uses `ndis5` upper range / `ethernet` lower range
- Same hardware IDs

### 3.3 mtkmbimx_x64.sys — NDIS 5.1 Miniport
- Same as above but targets **Windows XP** (NTamd64.5.1)
- Uses `ndis5` upper range / `ethernet` lower range
- Source from `net/ndis5/` directory
- Same hardware IDs

---

## 4. Certificate / Catalog Analysis

All `.cat` files are **PKCS#7 DER-encoded** digital signatures created by Microsoft WHQL:

| Catalog | Signed By | Valid Period |
|---------|-----------|--------------|
| `cdc-acm.cat` | Microsoft Time-Stamp Service (BBEC-30CA-2DBE) | May 2016 – Aug 2017 |
| `mtkmbim7_x64.cat` | Microsoft Time-Stamp Service (7D2E-3782-B0F7) | Oct 2015 – Jan 2017 |
| `mtkmbimv_x64.cat` | Microsoft Time-Stamp Service (BBEC-30CA-2DBE) | Oct 2015 – Jan 2017 |
| `mtkmbimx_x64.cat` | Microsoft Time-Stamp Service (BBEC-30CA-2DBE) | Oct 2015 – Jan 2017 |

All signed through **Microsoft PCA (Public Certificate Authority)** chain, indicating they passed WHQL certification.

---

## 5. Summary: Architecture of Original Proprietary Driver

```
┌─────────────────────────────────────────────────┐
│              Windows Application                 │
│         (SP Flash Tool, mtkclient, etc.)         │
├─────────────────────────────────────────────────┤
│             Win32 COM Port API                   │
│     (CreateFile, ReadFile, WriteFile, IOCTL)     │
├─────────────────────────────────────────────────┤
│           usb2ser.sys (WDM Driver)               │
│  ┌─────────────┬───────────┬──────────────────┐ │
│  │ FileOp      │ Serial    │ Power Mgmt       │ │
│  │ Create/Close│ IOCTLs    │ Idle/Suspend     │ │
│  ├─────────────┼───────────┼──────────────────┤ │
│  │ Data IN     │ Data OUT  │ Interrupt IN     │ │
│  │ (Bulk Read) │(Bulk Write)│ (Serial State)  │ │
│  ├─────────────┴───────────┴──────────────────┤ │
│  │              PnP Subsystem                  │ │
│  │  Start/Stop/Remove/SurpriseRemoval          │ │
│  └─────────────────────────────────────────────┘ │
├─────────────────────────────────────────────────┤
│         USB Driver Stack (USBD.SYS)              │
├─────────────────────────────────────────────────┤
│         USB Host Controller Driver               │
├─────────────────────────────────────────────────┤
│            Physical USB Hardware                 │
│        MediaTek Device (VID 0x0E8D)              │
│  BROM(0003) / Preloader(2000) / DA(2001) / ...  │
└─────────────────────────────────────────────────┘
```

### Key Finding

The proprietary `usb2ser.sys` implements a standard **USB CDC ACM** class driver.
Windows 10/11 includes a built-in inbox driver `usbser.sys` that provides
**identical CDC ACM functionality**. The opensource replacement only needs an
INF file to map MTK hardware IDs to the inbox `usbser.sys` driver — no custom
kernel binary is required.
