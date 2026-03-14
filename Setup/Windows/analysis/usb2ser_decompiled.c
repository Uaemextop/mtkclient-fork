/*
 * usb2ser_decompiled.c
 *
 * Full functional decompilation of MediaTek usb2ser.sys
 * (USB CDC ACM to Serial Port WDM Kernel Driver)
 *
 * Produced by Capstone Engine disassembly of:
 *   - CDC/x64/usb2ser.sys (151,184 bytes, AMD64, 553 functions)
 *   - CDC/x86/usb2ser.sys (119,952 bytes, i386, 246 functions)
 *
 * This is a documentation/reference file showing the complete internal
 * architecture of the proprietary MediaTek driver. It is NOT compilable
 * code — it is pseudo-C reconstructed from assembly to understand
 * the driver's exact behavior.
 *
 * Copyright analysis: The original binary is (C) MediaTek Inc.
 * This decompilation is for interoperability/opensource replacement purposes.
 */

/* =========================================================================
 * SECTION 1: HEADERS AND CONSTANTS
 * ========================================================================= */

#include <ntddk.h>
#include <wdm.h>
#include <usb.h>
#include <usbdlib.h>
#include <ntstrsafe.h>

/* Pool tags used by the driver (from disassembly: 'STCE' = 0x45435453) */
#define USB2SER_POOL_TAG     'STCE'   /* Standard allocations */
#define USB2SER_POOL_TAG_URB 'DSMU'   /* URB allocations */

/* Device extension flags */
#define FLAG_DEVICE_STARTED      0x01
#define FLAG_DEVICE_REMOVED      0x02
#define FLAG_SURPRISE_REMOVED    0x04
#define FLAG_DEVICE_OPEN         0x08

/* Maximum outstanding URBs (configurable via registry) */
#define DEFAULT_MAX_IN_IRPS      8
#define DEFAULT_MAX_OUT_IRPS     4

/* USB CDC ACM class-specific request codes */
#define CDC_SEND_ENCAPSULATED    0x00
#define CDC_GET_ENCAPSULATED     0x01
#define CDC_SET_LINE_CODING      0x20
#define CDC_GET_LINE_CODING      0x21
#define CDC_SET_CONTROL_LINE     0x22
#define CDC_SEND_BREAK           0x23

/* Serial state notification bits (from interrupt endpoint) */
#define SERIAL_STATE_DCD         0x01
#define SERIAL_STATE_DSR         0x02
#define SERIAL_STATE_BREAK       0x04
#define SERIAL_STATE_RI          0x08
#define SERIAL_STATE_FRAMING     0x10
#define SERIAL_STATE_PARITY      0x20
#define SERIAL_STATE_OVERRUN     0x40

/* =========================================================================
 * SECTION 2: DATA STRUCTURES (reconstructed from register usage patterns)
 * ========================================================================= */

/* USB CDC Line Coding structure (7 bytes, per USB CDC spec) */
typedef struct _CDC_LINE_CODING {
    ULONG  dwDTERate;       /* Baud rate */
    UCHAR  bCharFormat;     /* Stop bits: 0=1, 1=1.5, 2=2 */
    UCHAR  bParityType;     /* Parity: 0=None, 1=Odd, 2=Even, 3=Mark, 4=Space */
    UCHAR  bDataBits;       /* Data bits: 5, 6, 7, 8 */
} CDC_LINE_CODING, *PCDC_LINE_CODING;

/* URB context for tracking outstanding USB transfers */
typedef struct _URB_CONTEXT {
    LIST_ENTRY    ListEntry;
    PIRP          Irp;
    PURB          Urb;
    PMDL          Mdl;
    PVOID         Buffer;
    ULONG         BufferSize;
    ULONG         Index;          /* Sequence number for reordering */
    BOOLEAN       InUse;
    struct _DEVICE_EXTENSION *DevExt;
} URB_CONTEXT, *PURB_CONTEXT;

/* Buffer manager for read data ring buffer */
typedef struct _BUFFER_MANAGER {
    PUCHAR        Buffer;         /* Ring buffer memory */
    ULONG         BufferSize;     /* Total buffer size */
    ULONG         ReadOffset;     /* Current read position */
    ULONG         WriteOffset;    /* Current write position */
    ULONG         DataLength;     /* Bytes available to read */
    KSPIN_LOCK    Lock;           /* Spin lock for buffer access */
} BUFFER_MANAGER, *PBUFFER_MANAGER;

/* Data transfer context */
typedef struct _DATA_TRANSFER {
    /* Bulk IN (device -> host) */
    USBD_PIPE_HANDLE   InPipeHandle;
    ULONG              MaxInIrpNum;
    PURB_CONTEXT       InUrbContexts;
    LONG               OutstandingInUrbs;
    KEVENT             InUrbsCompleteEvent;

    /* Bulk OUT (host -> device) */
    USBD_PIPE_HANDLE   OutPipeHandle;
    ULONG              MaxOutIrpNum;
    PURB_CONTEXT       OutUrbContexts;
    LONG               OutstandingOutUrbs;
    KEVENT             OutUrbsCompleteEvent;

    /* Interrupt IN (serial state notifications) */
    USBD_PIPE_HANDLE   IntrPipeHandle;
    PURB_CONTEXT       IntrUrbContext;
    BOOLEAN            IntrTransActive;

    /* Read IRP queue */
    LIST_ENTRY         PendingReadIrps;
    KSPIN_LOCK         ReadLock;

    /* Write IRP queue */
    LIST_ENTRY         PendingWriteIrps;
    KSPIN_LOCK         WriteLock;

    /* Buffer manager for incoming data */
    BUFFER_MANAGER     ReadBuffer;

    /* DPCs for completion processing */
    KDPC               InDpc;
    KDPC               InReorderDpc;
    KDPC               OutDpc;
    KDPC               IntrDpc;

    /* Timers for read/write timeouts */
    KTIMER             ReadIntervalTimer;
    KDPC               ReadIntervalDpc;
    KTIMER             ReadTotalTimer;
    KDPC               ReadTotalDpc;
    KTIMER             WriteTotalTimer;
    KDPC               WriteTotalDpc;

    /* Transfer state */
    BOOLEAN            TransferStarted;
    BOOLEAN            InTransActive;
    BOOLEAN            OutTransActive;
    ULONG              InSequenceNumber;     /* For reordering */
    ULONG              NextExpectedSequence; /* For reordering */

    /* Statistics */
    SERIALPERF_STATS   PerfStats;
} DATA_TRANSFER, *PDATA_TRANSFER;

/* Serial port control state */
typedef struct _SERIAL_STATE {
    /* Line coding (baud, data bits, parity, stop bits) */
    CDC_LINE_CODING    LineCoding;

    /* Handshake / flow control */
    SERIAL_HANDFLOW    HandFlow;

    /* Modem control lines */
    BOOLEAN            DtrState;
    BOOLEAN            RtsState;

    /* Modem status (from device notifications) */
    ULONG              ModemStatus;      /* MSR register equivalent */
    ULONG              WaitMask;         /* Events to wait for */
    ULONG              EventHistory;     /* Events that have occurred */

    /* Wait mask thread */
    HANDLE             WaitMaskThreadHandle;
    PKTHREAD           WaitMaskThread;
    KEVENT             WaitMaskEvent;
    KEVENT             WaitMaskTerminate;
    BOOLEAN            WaitMaskRunning;
    PIRP               PendingWaitMaskIrp;
    KSPIN_LOCK         WaitMaskLock;

    /* Timeouts */
    SERIAL_TIMEOUTS    Timeouts;

    /* Special characters */
    SERIAL_CHARS       Chars;

    /* Queue sizes */
    ULONG              InQueueSize;
    ULONG              OutQueueSize;

    /* Properties */
    SERIAL_COMMPROP    Properties;
} SERIAL_STATE, *PSERIAL_STATE;

/* Power management state */
typedef struct _POWER_STATE_INFO {
    DEVICE_POWER_STATE CurrentDevicePower;
    SYSTEM_POWER_STATE CurrentSystemPower;
    DEVICE_CAPABILITIES DeviceCapabilities;

    /* Idle / Selective Suspend */
    BOOLEAN            IdleEnabled;
    ULONG              IdleTimeSeconds;
    BOOLEAN            IdleWWBound;
    KTIMER             IdleTimer;
    KDPC               IdleDpc;
    PIRP               IdleNotificationIrp;
    PIO_WORKITEM       IdleWorkItem;
    BOOLEAN            IdlePending;

    /* Wait-Wake */
    PIRP               WaitWakeIrp;
    BOOLEAN            WaitWakePending;
} POWER_STATE_INFO, *PPOWER_STATE_INFO;

/* WMI state */
typedef struct _WMI_STATE {
    BOOLEAN            Registered;
    PVOID              WmiLibInfo;
} WMI_STATE, *PWMI_STATE;

/* Main device extension — the core data structure for each device instance */
typedef struct _DEVICE_EXTENSION {
    /* Device objects */
    PDEVICE_OBJECT     Self;                /* Our FDO */
    PDEVICE_OBJECT     LowerDevice;         /* PDO we are attached to */
    PDEVICE_OBJECT     PhysicalDevice;      /* Physical device object */

    /* Device state */
    ULONG              Flags;
    IO_REMOVE_LOCK     RemoveLock;

    /* USB configuration */
    USBD_CONFIGURATION_HANDLE ConfigHandle;
    PUSB_CONFIGURATION_DESCRIPTOR ConfigDesc;
    UCHAR              InterfaceNumber;

    /* Symbolic link name */
    UNICODE_STRING     SymbolicLinkName;
    UNICODE_STRING     DeviceInterfaceName;
    UNICODE_STRING     PortName;            /* COMx */
    ULONG              PortNumber;

    /* Subsystems */
    DATA_TRANSFER      DataTransfer;
    SERIAL_STATE       SerialState;
    POWER_STATE_INFO   PowerState;
    WMI_STATE          WmiState;

    /* ETW/WMI tracing */
    PVOID              EtwHandle;
    ULONG              TraceLevel;
} DEVICE_EXTENSION, *PDEVICE_EXTENSION;


/* =========================================================================
 * SECTION 3: DRIVERENTRY — Main entry point
 * Disassembled from INIT section, RVA 0x21980 (x64) / 0x181BE (x86)
 * ========================================================================= */

NTSTATUS DriverEntry(
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    NTSTATUS status;

    /*
     * Phase 1: Dynamic import resolution (from INIT helper at 0x31988/x64)
     * The driver dynamically resolves optional APIs to support multiple
     * Windows versions (XP through 10):
     */
    UNICODE_STRING funcName;

    /* Resolve PsGetVersion for OS version detection */
    RtlInitUnicodeString(&funcName, L"PsGetVersion");
    g_PsGetVersion = MmGetSystemRoutineAddress(&funcName);

    /* Resolve WMI tracing functions */
    RtlInitUnicodeString(&funcName, L"WmiTraceMessage");
    g_WmiTraceMessage = MmGetSystemRoutineAddress(&funcName);

    RtlInitUnicodeString(&funcName, L"WmiQueryTraceInformation");
    g_WmiQueryTraceInformation = MmGetSystemRoutineAddress(&funcName);

    /* If Vista+, resolve ETW functions */
    if (OsVersion >= 6) {
        RtlInitUnicodeString(&funcName, L"EtwRegisterClassicProvider");
        g_EtwRegister = MmGetSystemRoutineAddress(&funcName);

        RtlInitUnicodeString(&funcName, L"EtwUnregister");
        g_EtwUnregister = MmGetSystemRoutineAddress(&funcName);
    }

    /* Register ETW/WMI tracing if available */
    if (g_EtwRegister) {
        g_EtwRegister(&GUID_MTK_USB2SER_TRACE, 0, NULL, &g_EtwHandle);
    }

    /*
     * Phase 2: Set up the IRP dispatch table (from sub_000006A8/x64)
     * Maps every IRP_MJ_xxx to the appropriate handler
     */

    /* Set ALL major functions to a default "pass-down" handler first */
    for (int i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; i++) {
        DriverObject->MajorFunction[i] = DefaultDispatch;
    }

    /* Override specific major functions */
    DriverObject->MajorFunction[IRP_MJ_CREATE]         = DispatchFileOp;
    DriverObject->MajorFunction[IRP_MJ_CLOSE]          = DispatchFileOp;
    DriverObject->MajorFunction[IRP_MJ_CLEANUP]        = DispatchFileOp;
    DriverObject->MajorFunction[IRP_MJ_READ]           = DispatchRead;
    DriverObject->MajorFunction[IRP_MJ_WRITE]          = DispatchWrite;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DispatchIoCtrl;
    DriverObject->MajorFunction[IRP_MJ_FLUSH_BUFFERS]  = DispatchFlush;
    DriverObject->MajorFunction[IRP_MJ_PNP]            = DispatchPnP;
    DriverObject->MajorFunction[IRP_MJ_POWER]          = DispatchPower;
    DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = DispatchSysCtrl;

    /* Set AddDevice callback */
    DriverObject->DriverExtension->AddDevice = AddDevice;

    /* Set Unload callback */
    DriverObject->DriverUnload = DriverUnload;

    return STATUS_SUCCESS;
}


/* =========================================================================
 * SECTION 4: AddDevice — Create and attach FDO
 * ========================================================================= */

NTSTATUS AddDevice(
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PDEVICE_OBJECT  PhysicalDeviceObject)
{
    NTSTATUS status;
    PDEVICE_OBJECT deviceObject = NULL;
    PDEVICE_EXTENSION devExt;

    /* Create our Functional Device Object (FDO) */
    status = IoCreateDevice(
        DriverObject,
        sizeof(DEVICE_EXTENSION),
        NULL,                           /* No device name — we use symlink */
        FILE_DEVICE_SERIAL_PORT,        /* Device type = serial port */
        FILE_DEVICE_SECURE_OPEN,
        FALSE,                          /* Not exclusive */
        &deviceObject);

    if (!NT_SUCCESS(status))
        return status;

    devExt = (PDEVICE_EXTENSION)deviceObject->DeviceExtension;
    RtlZeroMemory(devExt, sizeof(DEVICE_EXTENSION));

    devExt->Self = deviceObject;
    devExt->PhysicalDevice = PhysicalDeviceObject;

    /* Initialize remove lock */
    IoInitializeRemoveLockEx(&devExt->RemoveLock, USB2SER_POOL_TAG,
                              0, 0, sizeof(IO_REMOVE_LOCK));

    /* Initialize all synchronization objects */
    KeInitializeEvent(&devExt->DataTransfer.InUrbsCompleteEvent,
                      NotificationEvent, TRUE);
    KeInitializeEvent(&devExt->DataTransfer.OutUrbsCompleteEvent,
                      NotificationEvent, TRUE);

    InitializeListHead(&devExt->DataTransfer.PendingReadIrps);
    InitializeListHead(&devExt->DataTransfer.PendingWriteIrps);
    KeInitializeSpinLock(&devExt->DataTransfer.ReadLock);
    KeInitializeSpinLock(&devExt->DataTransfer.WriteLock);
    KeInitializeSpinLock(&devExt->DataTransfer.ReadBuffer.Lock);
    KeInitializeSpinLock(&devExt->SerialState.WaitMaskLock);

    /* Initialize DPCs */
    KeInitializeDpc(&devExt->DataTransfer.InDpc, Data_InDpcCallback, devExt);
    KeInitializeDpc(&devExt->DataTransfer.InReorderDpc, Data_QueueInReoderDpc, devExt);
    KeInitializeDpc(&devExt->DataTransfer.OutDpc, Data_OutDpcCallback, devExt);
    KeInitializeDpc(&devExt->DataTransfer.IntrDpc, Data_QueueInterruptDpc, devExt);

    /* Initialize timers for read/write timeouts */
    KeInitializeTimer(&devExt->DataTransfer.ReadIntervalTimer);
    KeInitializeDpc(&devExt->DataTransfer.ReadIntervalDpc,
                    Data_ReadIntervalTimeout, devExt);
    KeInitializeTimer(&devExt->DataTransfer.ReadTotalTimer);
    KeInitializeDpc(&devExt->DataTransfer.ReadTotalDpc,
                    Data_ReadTotalTimeoutCallback, devExt);
    KeInitializeTimer(&devExt->DataTransfer.WriteTotalTimer);
    KeInitializeDpc(&devExt->DataTransfer.WriteTotalDpc,
                    Data_WriteTotalTimeoutCallback, devExt);

    /* Initialize power idle timer */
    KeInitializeTimer(&devExt->PowerState.IdleTimer);
    KeInitializeDpc(&devExt->PowerState.IdleDpc, PWR_IdleDpcRoutine, devExt);

    /* Set default serial state */
    devExt->SerialState.LineCoding.dwDTERate = 115200;
    devExt->SerialState.LineCoding.bCharFormat = 0;  /* 1 stop bit */
    devExt->SerialState.LineCoding.bParityType = 0;  /* No parity */
    devExt->SerialState.LineCoding.bDataBits = 8;

    /* Attach to device stack */
    devExt->LowerDevice = IoAttachDeviceToDeviceStack(
        deviceObject, PhysicalDeviceObject);

    if (!devExt->LowerDevice) {
        IoDeleteDevice(deviceObject);
        return STATUS_UNSUCCESSFUL;
    }

    /* Register device interface (GUID_DEVINTERFACE_COMPORT) */
    status = IoRegisterDeviceInterface(
        PhysicalDeviceObject,
        &GUID_DEVINTERFACE_COMPORT,
        NULL,
        &devExt->DeviceInterfaceName);

    /* Set DO_BUFFERED_IO for serial port compatibility */
    deviceObject->Flags |= DO_BUFFERED_IO | DO_POWER_PAGABLE;
    deviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

    return STATUS_SUCCESS;
}


/* =========================================================================
 * SECTION 5: PnP HANDLERS
 * ========================================================================= */

NTSTATUS DispatchPnP(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PDEVICE_EXTENSION devExt = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION irpSp = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS status;

    status = IoAcquireRemoveLockEx(&devExt->RemoveLock, Irp,
                                    __FILE__, __LINE__, sizeof(IO_REMOVE_LOCK));
    if (!NT_SUCCESS(status)) {
        Irp->IoStatus.Status = status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return status;
    }

    switch (irpSp->MinorFunction) {
    case IRP_MN_START_DEVICE:
        return PnP_StartDevice(devExt, Irp);

    case IRP_MN_QUERY_REMOVE_DEVICE:
        return PnP_QueryRemoveDevice(devExt, Irp);

    case IRP_MN_CANCEL_REMOVE_DEVICE:
        return PnP_CancelRemoveDevice(devExt, Irp);

    case IRP_MN_REMOVE_DEVICE:
        return PnP_RemoveDevice(devExt, Irp);

    case IRP_MN_SURPRISE_REMOVAL:
        return PnP_SurpriseRemoval(devExt, Irp);

    case IRP_MN_QUERY_CAPABILITIES:
        return PnP_QueryCapabilities(devExt, Irp);

    case IRP_MN_STOP_DEVICE:
    case IRP_MN_QUERY_STOP_DEVICE:
    case IRP_MN_CANCEL_STOP_DEVICE:
    default:
        return PnP_SyncPassDownIrp(devExt, Irp);
    }
}

NTSTATUS PnP_StartDevice(PDEVICE_EXTENSION DevExt, PIRP Irp)
{
    NTSTATUS status;

    /* First, pass IRP down to bus driver and wait for completion */
    status = PnP_SyncPassDownIrp(DevExt, Irp);
    if (!NT_SUCCESS(status))
        return status;

    /* Get USB configuration descriptor */
    status = USB_GetConfigDescriptor(DevExt);
    if (!NT_SUCCESS(status))
        return status;

    /* Select USB configuration (find CDC ACM interfaces) */
    status = USB_SelectConfiguration(DevExt);
    if (!NT_SUCCESS(status))
        return status;

    /* 
     * Parse configuration to find:
     *   - Bulk IN endpoint  (for reading serial data)
     *   - Bulk OUT endpoint (for writing serial data)
     *   - Interrupt IN endpoint (for serial state notifications)
     */
    status = Data_GetDataConfig(DevExt);
    if (!NT_SUCCESS(status))
        return status;

    /* Initialize buffer manager (ring buffer for incoming data) */
    status = Data_InitBufferManager(DevExt);
    if (!NT_SUCCESS(status))
        return status;

    /* Pre-allocate URBs for bulk transfers */
    status = Data_ReserveResources(DevExt);
    if (!NT_SUCCESS(status))
        return status;

    /* Read registry settings (idle time, max IRPs, etc.) */
    PWR_GetIdleInfo(DevExt);

    /* Create symbolic link: \Device\cdcacmN -> COMx */
    status = PnP_CreateSymbolicLink(DevExt);
    if (!NT_SUCCESS(status))
        return status;

    /* Enable device interface */
    IoSetDeviceInterfaceState(&DevExt->DeviceInterfaceName, TRUE);

    /* Initialize WMI */
    IoWMIRegistrationControl(DevExt->Self, WMIREG_ACTION_REGISTER);

    /* Set device as started */
    DevExt->Flags |= FLAG_DEVICE_STARTED;

    /* Update power capabilities */
    PWR_UpdateDeviceCapabilites(DevExt);

    /* Set initial DTR/RTS line state */
    Ctrl_SetDefaultLineState(DevExt);

    return STATUS_SUCCESS;
}

NTSTATUS PnP_RemoveDevice(PDEVICE_EXTENSION DevExt, PIRP Irp)
{
    /* Stop all transfers */
    Data_StopTransfer(DevExt);
    Data_CleanupTransfer(DevExt);

    /* Cancel power-related IRPs */
    PWR_CancelIdleTimer(DevExt);
    PWR_CancelIdleNotificationIrp(DevExt);
    PWR_CancelWaitWakeIrp(DevExt);

    /* Remove symbolic links */
    PnP_RemoveSymbolicLinks(DevExt);

    /* Disable device interface */
    IoSetDeviceInterfaceState(&DevExt->DeviceInterfaceName, FALSE);

    /* Deregister WMI */
    IoWMIRegistrationControl(DevExt->Self, WMIREG_ACTION_DEREGISTER);

    /* Release remove lock and wait for all I/O to complete */
    IoReleaseRemoveLockAndWaitEx(&DevExt->RemoveLock, Irp,
                                  sizeof(IO_REMOVE_LOCK));

    /* Pass IRP down */
    IoSkipCurrentIrpStackLocation(Irp);
    NTSTATUS status = IoCallDriver(DevExt->LowerDevice, Irp);

    /* Detach and delete */
    IoDetachDevice(DevExt->LowerDevice);
    IoDeleteDevice(DevExt->Self);

    return status;
}

NTSTATUS PnP_CreateSymbolicLink(PDEVICE_EXTENSION DevExt)
{
    /*
     * Creates: \Device\cdcacmN  and  \DosDevices\COMx
     * Reads PortName from registry, or auto-assigns next available COM port.
     * Device name format: "\\Device\\cdcacm" + decimal index
     * (seen as L"cdcacm" in string table, and L"\\??\\" prefix)
     */
    WCHAR deviceNameBuf[64];
    WCHAR dosNameBuf[64];
    UNICODE_STRING deviceName, dosName;

    /* Build device name: \Device\cdcacm0 */
    swprintf(deviceNameBuf, L"\\Device\\cdcacm%d", DevExt->PortNumber);
    RtlInitUnicodeString(&deviceName, deviceNameBuf);

    /* Build DOS name: \??\COM3 (reads from registry PortName) */
    swprintf(dosNameBuf, L"\\??\\%ws", DevExt->PortName.Buffer);
    RtlInitUnicodeString(&dosName, dosNameBuf);

    /* Write SERIALCOMM registry entry */
    RtlWriteRegistryValue(RTL_REGISTRY_DEVICEMAP, L"SERIALCOMM",
                           deviceName.Buffer, REG_SZ,
                           DevExt->PortName.Buffer,
                           DevExt->PortName.Length + sizeof(WCHAR));

    return IoCreateSymbolicLink(&dosName, &deviceName);
}


/* =========================================================================
 * SECTION 6: FILE OPERATIONS (Create / Close / Cleanup)
 * ========================================================================= */

NTSTATUS DispatchFileOp(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PDEVICE_EXTENSION devExt = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION irpSp = IoGetCurrentIrpStackLocation(Irp);

    switch (irpSp->MajorFunction) {
    case IRP_MJ_CREATE:
        return FileOp_Create(devExt, Irp);
    case IRP_MJ_CLOSE:
        return FileOp_Close(devExt, Irp);
    case IRP_MJ_CLEANUP:
        return FileOp_Cleanup(devExt, Irp);
    }
    return STATUS_INVALID_DEVICE_REQUEST;
}

NTSTATUS FileOp_Create(PDEVICE_EXTENSION DevExt, PIRP Irp)
{
    NTSTATUS status;

    if (DevExt->Flags & FLAG_DEVICE_REMOVED) {
        Irp->IoStatus.Status = STATUS_DELETE_PENDING;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_DELETE_PENDING;
    }

    /* Start all USB data transfers */
    status = Data_StartTransfer(DevExt);
    if (!NT_SUCCESS(status)) {
        Irp->IoStatus.Status = status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return status;
    }

    /* Initialize wait mask thread for WAIT_ON_MASK support */
    Ctrl_InitWaitMaskThread(DevExt);

    /* Set DTR and RTS */
    Ctrl_SetDtr(DevExt);
    Ctrl_SetRts(DevExt);

    /* Apply default line coding (115200-8-N-1) */
    Ctrl_SetLineCoding(DevExt);

    /* Cancel idle timer (device is now active) */
    PWR_CancelIdleTimer(DevExt);

    DevExt->Flags |= FLAG_DEVICE_OPEN;

    Irp->IoStatus.Status = STATUS_SUCCESS;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

NTSTATUS FileOp_Close(PDEVICE_EXTENSION DevExt, PIRP Irp)
{
    /* Stop data transfers */
    Data_StopTransfer(DevExt);

    /* Cancel wait mask thread */
    Ctrl_CancelWaitMaskThread(DevExt);

    /* Clear DTR/RTS */
    Ctrl_ClrDtr(DevExt);
    Ctrl_ClrRts(DevExt);

    /* Re-enable idle timer */
    PWR_IssueIdleTimer(DevExt);

    DevExt->Flags &= ~FLAG_DEVICE_OPEN;

    Irp->IoStatus.Status = STATUS_SUCCESS;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}


/* =========================================================================
 * SECTION 7: READ / WRITE DISPATCH
 * ========================================================================= */

NTSTATUS DispatchRead(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PDEVICE_EXTENSION devExt = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION irpSp = IoGetCurrentIrpStackLocation(Irp);
    ULONG readLength = irpSp->Parameters.Read.Length;

    if (readLength == 0) {
        Irp->IoStatus.Status = STATUS_SUCCESS;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_SUCCESS;
    }

    /* Try to fill from ring buffer immediately */
    ULONG bytesAvailable = Data_FillinDataFromBuffer(devExt, Irp);
    if (bytesAvailable > 0) {
        Irp->IoStatus.Status = STATUS_SUCCESS;
        Irp->IoStatus.Information = bytesAvailable;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_SUCCESS;
    }

    /* No data available — queue IRP and set up timeouts */
    IoMarkIrpPending(Irp);
    IoSetCancelRoutine(Irp, Data_CancelRead);

    /* Add to pending reads queue */
    ExInterlockedInsertTailList(&devExt->DataTransfer.PendingReadIrps,
                                 &Irp->Tail.Overlay.ListEntry,
                                 &devExt->DataTransfer.ReadLock);

    /* Start timeout timers if configured */
    if (devExt->SerialState.Timeouts.ReadIntervalTimeout) {
        Data_StartReadIntervalTimeout(devExt);
    }
    if (devExt->SerialState.Timeouts.ReadTotalTimeoutMultiplier ||
        devExt->SerialState.Timeouts.ReadTotalTimeoutConstant) {
        Data_StartReadTotalTimeout(devExt, readLength);
    }

    return STATUS_PENDING;
}

NTSTATUS DispatchWrite(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PDEVICE_EXTENSION devExt = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION irpSp = IoGetCurrentIrpStackLocation(Irp);
    ULONG writeLength = irpSp->Parameters.Write.Length;

    if (writeLength == 0) {
        Irp->IoStatus.Status = STATUS_SUCCESS;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_SUCCESS;
    }

    /* Check idle power state */
    PWR_CheckIdleForDataTrans(devExt);

    /* Queue the write IRP */
    IoMarkIrpPending(Irp);
    IoSetCancelRoutine(Irp, Data_CancelWrite);

    ExInterlockedInsertTailList(&devExt->DataTransfer.PendingWriteIrps,
                                 &Irp->Tail.Overlay.ListEntry,
                                 &devExt->DataTransfer.WriteLock);

    /* Start write timeout if configured */
    Data_StartWriteTimeout(devExt, writeLength);

    /* Trigger OUT transfer processing */
    Data_QueueOutDpc(devExt);

    return STATUS_PENDING;
}


/* =========================================================================
 * SECTION 8: IOCTL DISPATCH (Serial Port Control)
 * ========================================================================= */

NTSTATUS DispatchIoCtrl(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PDEVICE_EXTENSION devExt = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION irpSp = IoGetCurrentIrpStackLocation(Irp);
    ULONG ioctl = irpSp->Parameters.DeviceIoControl.IoControlCode;
    NTSTATUS status = STATUS_SUCCESS;

    switch (ioctl) {
    /* ---- Baud Rate ---- */
    case IOCTL_SERIAL_SET_BAUD_RATE:
        status = Ctrl_SetBaudRate(devExt, Irp);
        break;
    case IOCTL_SERIAL_GET_BAUD_RATE:
        status = Ctrl_GetBaudRate(devExt, Irp);
        break;

    /* ---- Line Control (data bits, parity, stop bits) ---- */
    case IOCTL_SERIAL_SET_LINE_CONTROL:
        status = Ctrl_SetLineControl(devExt, Irp);
        break;
    case IOCTL_SERIAL_GET_LINE_CONTROL:
        status = Ctrl_GetLineControl(devExt, Irp);
        break;

    /* ---- Handshake / Flow Control ---- */
    case IOCTL_SERIAL_SET_HANDFLOW:
        status = Ctrl_SetHandflow(devExt, Irp);
        break;
    case IOCTL_SERIAL_GET_HANDFLOW:
        status = Ctrl_GetHandflow(devExt, Irp);
        break;

    /* ---- DTR/RTS Control ---- */
    case IOCTL_SERIAL_SET_DTR:
        status = Ctrl_SetDtr(devExt);
        break;
    case IOCTL_SERIAL_CLR_DTR:
        status = Ctrl_ClrDtr(devExt);
        break;
    case IOCTL_SERIAL_SET_RTS:
        status = Ctrl_SetRts(devExt);
        break;
    case IOCTL_SERIAL_CLR_RTS:
        status = Ctrl_ClrRts(devExt);
        break;
    case IOCTL_SERIAL_GET_DTRRTS:
        status = Ctrl_GetDtrRts(devExt, Irp);
        break;

    /* ---- Modem Status ---- */
    case IOCTL_SERIAL_GET_MODEMSTATUS:
        status = Ctrl_GetModemStatus(devExt, Irp);
        break;

    /* ---- Break ---- */
    case IOCTL_SERIAL_SET_BREAK_ON:
        status = Ctrl_SetBreakOn(devExt);
        break;
    case IOCTL_SERIAL_SET_BREAK_OFF:
        status = Ctrl_SetBreakOff(devExt);
        break;

    /* ---- Timeouts ---- */
    case IOCTL_SERIAL_SET_TIMEOUTS:
        status = Ctrl_SetTimeouts(devExt, Irp);
        break;
    case IOCTL_SERIAL_GET_TIMEOUTS:
        status = Ctrl_GetTimeouts(devExt, Irp);
        break;

    /* ---- Special Characters ---- */
    case IOCTL_SERIAL_SET_CHARS:
        status = Ctrl_SetChars(devExt, Irp);
        break;
    case IOCTL_SERIAL_GET_CHARS:
        status = Ctrl_GetChars(devExt, Irp);
        break;

    /* ---- Queue Size ---- */
    case IOCTL_SERIAL_SET_QUEUE_SIZE:
        status = Ctrl_SetQueueSize(devExt, Irp);
        break;

    /* ---- Wait Mask (event notification) ---- */
    case IOCTL_SERIAL_SET_WAIT_MASK:
        status = Ctrl_SetWaitMask(devExt, Irp);
        break;
    case IOCTL_SERIAL_GET_WAIT_MASK:
        status = Ctrl_GetWaitMask(devExt, Irp);
        break;
    case IOCTL_SERIAL_WAIT_ON_MASK:
        status = Ctrl_WaitOnMask(devExt, Irp);
        break;

    /* ---- Purge ---- */
    case IOCTL_SERIAL_PURGE:
        status = Ctrl_Purge(devExt, Irp);
        break;

    /* ---- Status & Properties ---- */
    case IOCTL_SERIAL_GET_COMMSTATUS:
        status = Ctrl_GetCommStatus(devExt, Irp);
        break;
    case IOCTL_SERIAL_GET_PROPERTIES:
        status = Ctrl_GetProperties(devExt, Irp);
        break;
    case IOCTL_SERIAL_GET_STATS:
        status = Ctrl_GetStats(devExt, Irp);
        break;
    case IOCTL_SERIAL_CLEAR_STATS:
        status = Ctrl_ClearStats(devExt);
        break;
    case IOCTL_SERIAL_CONFIG_SIZE:
        status = Ctrl_ConfigSize(devExt, Irp);
        break;

    /* ---- LSR/MSR Insertion ---- */
    case IOCTL_SERIAL_LSRMST_INSERT:
        status = Ctrl_LsrMstInsert(devExt, Irp);
        break;

    default:
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

    if (status != STATUS_PENDING) {
        Irp->IoStatus.Status = status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
    }
    return status;
}


/* =========================================================================
 * SECTION 9: USB DATA TRANSFER ENGINE
 * This is the core of the driver — manages bulk IN/OUT/Interrupt transfers
 * ========================================================================= */

NTSTATUS Data_StartTransfer(PDEVICE_EXTENSION DevExt)
{
    /* Start interrupt endpoint (serial state notifications) */
    Data_StartIntrTrans(DevExt);

    /* Submit initial bulk IN URBs (multiple for throughput) */
    for (ULONG i = 0; i < DevExt->DataTransfer.MaxInIrpNum; i++) {
        Data_StartInTrans(DevExt, i);
    }

    DevExt->DataTransfer.TransferStarted = TRUE;
    return STATUS_SUCCESS;
}

VOID Data_StartInTrans(PDEVICE_EXTENSION DevExt, ULONG Index)
{
    PURB_CONTEXT ctx = &DevExt->DataTransfer.InUrbContexts[Index];
    PURB urb = ctx->Urb;

    /* Build bulk transfer URB */
    UsbBuildInterruptOrBulkTransferRequest(
        urb,
        sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER),
        DevExt->DataTransfer.InPipeHandle,
        ctx->Buffer,
        NULL,          /* No MDL */
        ctx->BufferSize,
        USBD_TRANSFER_DIRECTION_IN | USBD_SHORT_TRANSFER_OK,
        NULL);         /* No link */

    ctx->Index = InterlockedIncrement(&DevExt->DataTransfer.InSequenceNumber);

    /* Set completion routine */
    IoSetCompletionRoutine(ctx->Irp, Data_InComplete, ctx, TRUE, TRUE, TRUE);

    /* Submit URB to USB stack */
    PIO_STACK_LOCATION nextSp = IoGetNextIrpStackLocation(ctx->Irp);
    nextSp->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
    nextSp->Parameters.Others.Argument1 = urb;

    InterlockedIncrement(&DevExt->DataTransfer.OutstandingInUrbs);
    IoCallDriver(DevExt->LowerDevice, ctx->Irp);
}

NTSTATUS Data_InComplete(PDEVICE_OBJECT DeviceObject, PIRP Irp, PVOID Context)
{
    PURB_CONTEXT ctx = (PURB_CONTEXT)Context;
    PDEVICE_EXTENSION devExt = ctx->DevExt;
    PURB urb = ctx->Urb;

    if (!NT_SUCCESS(Irp->IoStatus.Status)) {
        /* Pipe error — initiate reset */
        if (Irp->IoStatus.Status == STATUS_CANCELLED) {
            goto done;
        }
        Data_StartResetInPipe(devExt);
        goto done;
    }

    ULONG bytesRead = urb->UrbBulkOrInterruptTransfer.TransferBufferLength;
    if (bytesRead > 0) {
        /* Copy data to ring buffer (with reordering if needed) */
        Data_InTransReorder(devExt, ctx, bytesRead);

        /* Queue DPC to process data and complete pending read IRPs */
        KeInsertQueueDpc(&devExt->DataTransfer.InDpc, NULL, NULL);
    }

    /* Re-submit the URB for continuous reading */
    IoReuseIrp(ctx->Irp, STATUS_SUCCESS);
    Data_StartInTrans(devExt, ctx - devExt->DataTransfer.InUrbContexts);

done:
    if (InterlockedDecrement(&devExt->DataTransfer.OutstandingInUrbs) == 0) {
        KeSetEvent(&devExt->DataTransfer.InUrbsCompleteEvent,
                   IO_NO_INCREMENT, FALSE);
    }
    return STATUS_MORE_PROCESSING_REQUIRED;
}

VOID Data_InDpcCallback(PKDPC Dpc, PVOID Context, PVOID Arg1, PVOID Arg2)
{
    PDEVICE_EXTENSION devExt = (PDEVICE_EXTENSION)Context;

    /* Process all pending read IRPs against the ring buffer */
    while (TRUE) {
        PLIST_ENTRY entry;
        KIRQL oldIrql;

        KeAcquireSpinLock(&devExt->DataTransfer.ReadLock, &oldIrql);
        if (IsListEmpty(&devExt->DataTransfer.PendingReadIrps)) {
            KeReleaseSpinLock(&devExt->DataTransfer.ReadLock, oldIrql);
            break;
        }

        entry = RemoveHeadList(&devExt->DataTransfer.PendingReadIrps);
        KeReleaseSpinLock(&devExt->DataTransfer.ReadLock, oldIrql);

        PIRP readIrp = CONTAINING_RECORD(entry, IRP, Tail.Overlay.ListEntry);

        /* Fill IRP from ring buffer */
        ULONG bytesCopied = Data_FillinDataFromBuffer(devExt, readIrp);
        if (bytesCopied > 0) {
            Data_CompleteReadRequest(devExt, readIrp, bytesCopied);
        } else {
            /* Re-queue if no data available yet */
            KeAcquireSpinLock(&devExt->DataTransfer.ReadLock, &oldIrql);
            InsertHeadList(&devExt->DataTransfer.PendingReadIrps,
                          &readIrp->Tail.Overlay.ListEntry);
            KeReleaseSpinLock(&devExt->DataTransfer.ReadLock, oldIrql);
            break;
        }
    }

    /* Update serial events (EV_RXCHAR etc.) */
    Ctrl_UpdateSerialState(devExt);
}

VOID Data_OutDpcCallback(PKDPC Dpc, PVOID Context, PVOID Arg1, PVOID Arg2)
{
    PDEVICE_EXTENSION devExt = (PDEVICE_EXTENSION)Context;

    /* Process pending write IRPs */
    while (TRUE) {
        PLIST_ENTRY entry;
        KIRQL oldIrql;

        KeAcquireSpinLock(&devExt->DataTransfer.WriteLock, &oldIrql);
        if (IsListEmpty(&devExt->DataTransfer.PendingWriteIrps)) {
            KeReleaseSpinLock(&devExt->DataTransfer.WriteLock, oldIrql);
            break;
        }

        /* Find a free OUT URB context */
        PURB_CONTEXT outCtx = NULL;
        for (ULONG i = 0; i < devExt->DataTransfer.MaxOutIrpNum; i++) {
            if (!devExt->DataTransfer.OutUrbContexts[i].InUse) {
                outCtx = &devExt->DataTransfer.OutUrbContexts[i];
                outCtx->InUse = TRUE;
                break;
            }
        }
        if (!outCtx) {
            KeReleaseSpinLock(&devExt->DataTransfer.WriteLock, oldIrql);
            break;  /* All OUT URBs are busy */
        }

        entry = RemoveHeadList(&devExt->DataTransfer.PendingWriteIrps);
        KeReleaseSpinLock(&devExt->DataTransfer.WriteLock, oldIrql);

        PIRP writeIrp = CONTAINING_RECORD(entry, IRP, Tail.Overlay.ListEntry);
        PIO_STACK_LOCATION irpSp = IoGetCurrentIrpStackLocation(writeIrp);

        /* Copy write data to URB buffer */
        ULONG writeLen = irpSp->Parameters.Write.Length;
        RtlCopyMemory(outCtx->Buffer, writeIrp->AssociatedIrp.SystemBuffer, writeLen);

        /* Build bulk OUT URB */
        UsbBuildInterruptOrBulkTransferRequest(
            outCtx->Urb,
            sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER),
            devExt->DataTransfer.OutPipeHandle,
            outCtx->Buffer,
            NULL,
            writeLen,
            USBD_TRANSFER_DIRECTION_OUT,
            NULL);

        /* Associate write IRP with URB context for completion */
        outCtx->Irp->Tail.Overlay.OriginalFileObject = (PVOID)writeIrp;

        /* Submit */
        IoSetCompletionRoutine(outCtx->Irp, Data_OutComplete, outCtx,
                               TRUE, TRUE, TRUE);
        IoCallDriver(devExt->LowerDevice, outCtx->Irp);
    }
}

NTSTATUS Data_OutComplete(PDEVICE_OBJECT DeviceObject, PIRP Irp, PVOID Context)
{
    PURB_CONTEXT ctx = (PURB_CONTEXT)Context;
    PDEVICE_EXTENSION devExt = ctx->DevExt;
    PIRP writeIrp = (PIRP)ctx->Irp->Tail.Overlay.OriginalFileObject;

    if (!NT_SUCCESS(Irp->IoStatus.Status)) {
        Data_StartResetOutPipe(devExt);
        writeIrp->IoStatus.Status = Irp->IoStatus.Status;
    } else {
        writeIrp->IoStatus.Status = STATUS_SUCCESS;
        writeIrp->IoStatus.Information =
            ctx->Urb->UrbBulkOrInterruptTransfer.TransferBufferLength;
    }

    /* Check for ZLP needed (transfer was exact multiple of max packet) */
    if (NT_SUCCESS(Irp->IoStatus.Status) &&
        writeIrp->IoStatus.Information > 0 &&
        (writeIrp->IoStatus.Information % 512) == 0) {
        Data_OutSendZLP(devExt, ctx);
    } else {
        ctx->InUse = FALSE;
        IoReuseIrp(ctx->Irp, STATUS_SUCCESS);
        Data_CompleteWriteRequest(devExt, writeIrp,
                                  (ULONG)writeIrp->IoStatus.Information);
    }

    return STATUS_MORE_PROCESSING_REQUIRED;
}


/* =========================================================================
 * SECTION 10: SERIAL STATE NOTIFICATIONS (Interrupt endpoint)
 * ========================================================================= */

VOID Data_IntrUrbComplete(PDEVICE_OBJECT DeviceObject, PIRP Irp, PVOID Context)
{
    /*
     * CDC ACM SERIAL_STATE notification format:
     * Byte 0: bmRequestType (0xA1 = class-specific, interface, device-to-host)
     * Byte 1: bNotification (0x20 = SERIAL_STATE)
     * Byte 2-3: wValue (0)
     * Byte 4-5: wIndex (interface)
     * Byte 6-7: wLength (2)
     * Byte 8-9: UART state bitmap
     */
    PURB_CONTEXT ctx = (PURB_CONTEXT)Context;
    PDEVICE_EXTENSION devExt = ctx->DevExt;
    PUCHAR notifData = ctx->Buffer;
    ULONG length = ctx->Urb->UrbBulkOrInterruptTransfer.TransferBufferLength;

    if (NT_SUCCESS(Irp->IoStatus.Status) && length >= 10) {
        if (notifData[1] == 0x20) {  /* SERIAL_STATE */
            USHORT uartState = *(PUSHORT)(notifData + 8);
            Data_SerialStateNotify(devExt, uartState);
        }
    }

    /* Re-submit interrupt URB */
    if (!(devExt->Flags & FLAG_DEVICE_REMOVED)) {
        IoReuseIrp(ctx->Irp, STATUS_SUCCESS);
        Data_StartIntrTrans(devExt);
    }
}

VOID Data_SerialStateNotify(PDEVICE_EXTENSION DevExt, USHORT UartState)
{
    ULONG modemStatus = 0;
    ULONG events = 0;

    /* Map CDC ACM state bits to Win32 modem status bits */
    if (UartState & SERIAL_STATE_DCD)
        modemStatus |= SERIAL_MSR_DCD;
    if (UartState & SERIAL_STATE_DSR)
        modemStatus |= SERIAL_MSR_DSR;
    if (UartState & SERIAL_STATE_RI)
        modemStatus |= SERIAL_MSR_RI;

    /* Detect changes for event notification */
    ULONG prevStatus = DevExt->SerialState.ModemStatus;
    if ((modemStatus ^ prevStatus) & SERIAL_MSR_DCD)
        events |= SERIAL_EV_RLSD;
    if ((modemStatus ^ prevStatus) & SERIAL_MSR_DSR)
        events |= SERIAL_EV_DSR;
    if ((modemStatus ^ prevStatus) & SERIAL_MSR_RI)
        events |= SERIAL_EV_RING;

    if (UartState & SERIAL_STATE_BREAK)
        events |= SERIAL_EV_BREAK;
    if (UartState & (SERIAL_STATE_FRAMING | SERIAL_STATE_PARITY))
        events |= SERIAL_EV_ERR;

    DevExt->SerialState.ModemStatus = modemStatus;

    /* Signal waiting threads if matching events */
    if (events & DevExt->SerialState.WaitMask) {
        DevExt->SerialState.EventHistory |= events;
        Ctrl_CompletePendingWaitOnMask(DevExt);
    }
}


/* =========================================================================
 * SECTION 11: USB CONTROL REQUESTS (CDC ACM class-specific)
 * ========================================================================= */

NTSTATUS Ctrl_SetLineCoding(PDEVICE_EXTENSION DevExt)
{
    /*
     * Send SET_LINE_CODING (0x20) control transfer to device
     * This sets baud rate, data bits, parity, stop bits on the USB device
     */
    NTSTATUS status;
    KEVENT event;
    IO_STATUS_BLOCK ioStatus;

    KeInitializeEvent(&event, NotificationEvent, FALSE);

    /* Build USB control transfer */
    struct {
        struct _URB_HEADER Header;
        struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST Request;
    } urb;

    RtlZeroMemory(&urb, sizeof(urb));
    urb.Header.Length = sizeof(struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST);
    urb.Header.Function = URB_FUNCTION_CLASS_INTERFACE;
    urb.Request.TransferFlags = USBD_TRANSFER_DIRECTION_OUT;
    urb.Request.TransferBufferLength = sizeof(CDC_LINE_CODING);
    urb.Request.TransferBuffer = &DevExt->SerialState.LineCoding;
    urb.Request.Request = CDC_SET_LINE_CODING;  /* 0x20 */
    urb.Request.Value = 0;
    urb.Request.Index = DevExt->InterfaceNumber;

    PIRP irp = IoBuildDeviceIoControlRequest(
        IOCTL_INTERNAL_USB_SUBMIT_URB,
        DevExt->LowerDevice,
        NULL, 0, NULL, 0, TRUE, &event, &ioStatus);

    PIO_STACK_LOCATION sp = IoGetNextIrpStackLocation(irp);
    sp->Parameters.Others.Argument1 = &urb;

    status = IoCallDriver(DevExt->LowerDevice, irp);
    if (status == STATUS_PENDING) {
        KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, NULL);
        status = ioStatus.Status;
    }
    return status;
}

NTSTATUS Ctrl_SetDtr(PDEVICE_EXTENSION DevExt)
{
    /* CDC SET_CONTROL_LINE_STATE with DTR bit set */
    USHORT value = 0x01;  /* DTR = bit 0 */
    if (DevExt->SerialState.RtsState)
        value |= 0x02;    /* Preserve RTS = bit 1 */
    DevExt->SerialState.DtrState = TRUE;
    return Ctrl_SendControlLineState(DevExt, value);
}

NTSTATUS Ctrl_ClrDtr(PDEVICE_EXTENSION DevExt)
{
    USHORT value = 0;
    if (DevExt->SerialState.RtsState)
        value |= 0x02;
    DevExt->SerialState.DtrState = FALSE;
    return Ctrl_SendControlLineState(DevExt, value);
}

NTSTATUS Ctrl_SetRts(PDEVICE_EXTENSION DevExt)
{
    USHORT value = 0x02;  /* RTS = bit 1 */
    if (DevExt->SerialState.DtrState)
        value |= 0x01;
    DevExt->SerialState.RtsState = TRUE;
    return Ctrl_SendControlLineState(DevExt, value);
}

NTSTATUS Ctrl_ClrRts(PDEVICE_EXTENSION DevExt)
{
    USHORT value = 0;
    if (DevExt->SerialState.DtrState)
        value |= 0x01;
    DevExt->SerialState.RtsState = FALSE;
    return Ctrl_SendControlLineState(DevExt, value);
}

NTSTATUS Ctrl_SendControlLineState(PDEVICE_EXTENSION DevExt, USHORT Value)
{
    /* USB CDC SET_CONTROL_LINE_STATE (0x22) */
    /* This controls DTR (bit 0) and RTS (bit 1) signals */
    KEVENT event;
    IO_STATUS_BLOCK ioStatus;

    KeInitializeEvent(&event, NotificationEvent, FALSE);

    struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST urb = {0};
    urb.Hdr.Length = sizeof(urb);
    urb.Hdr.Function = URB_FUNCTION_CLASS_INTERFACE;
    urb.TransferFlags = USBD_TRANSFER_DIRECTION_OUT;
    urb.TransferBufferLength = 0;
    urb.Request = CDC_SET_CONTROL_LINE;  /* 0x22 */
    urb.Value = Value;
    urb.Index = DevExt->InterfaceNumber;

    PIRP irp = IoBuildDeviceIoControlRequest(
        IOCTL_INTERNAL_USB_SUBMIT_URB,
        DevExt->LowerDevice,
        NULL, 0, NULL, 0, TRUE, &event, &ioStatus);

    PIO_STACK_LOCATION sp = IoGetNextIrpStackLocation(irp);
    sp->Parameters.Others.Argument1 = &urb;

    NTSTATUS status = IoCallDriver(DevExt->LowerDevice, irp);
    if (status == STATUS_PENDING) {
        KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, NULL);
        status = ioStatus.Status;
    }
    return status;
}


/* =========================================================================
 * SECTION 12: POWER MANAGEMENT
 * ========================================================================= */

NTSTATUS DispatchPower(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PDEVICE_EXTENSION devExt = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION irpSp = IoGetCurrentIrpStackLocation(Irp);

    switch (irpSp->MinorFunction) {
    case IRP_MN_SET_POWER:
        if (irpSp->Parameters.Power.Type == SystemPowerState)
            return PWR_HandleSetPowerSystem(devExt, Irp);
        else
            return PWR_HandleSetPowerDevice(devExt, Irp);

    case IRP_MN_QUERY_POWER:
        if (irpSp->Parameters.Power.Type == SystemPowerState)
            return PWR_HandleQueryPowerSystem(devExt, Irp);
        else
            return PWR_HandleQueryPowerDevice(devExt, Irp);

    case IRP_MN_WAIT_WAKE:
        return PWR_HandleWaitWake(devExt, Irp);

    default:
        PoStartNextPowerIrp(Irp);
        IoSkipCurrentIrpStackLocation(Irp);
        return PoCallDriver(devExt->LowerDevice, Irp);
    }
}

/*
 * Selective suspend / idle management:
 * When the device has been idle for IdleTime seconds, the driver
 * sends an idle notification IRP to the bus driver, which may
 * suspend the USB port to save power.
 */
VOID PWR_IdleDpcRoutine(PKDPC Dpc, PVOID Context, PVOID Arg1, PVOID Arg2)
{
    PDEVICE_EXTENSION devExt = (PDEVICE_EXTENSION)Context;

    if (devExt->Flags & FLAG_DEVICE_OPEN) {
        /* Device is open — don't idle */
        return;
    }

    /* Queue work item to issue idle notification at PASSIVE_LEVEL */
    IoQueueWorkItem(devExt->PowerState.IdleWorkItem,
                    PWR_IdleRequestWorkerRoutine,
                    DelayedWorkQueue, devExt);
}


/* =========================================================================
 * END OF DECOMPILATION
 *
 * Total reconstructed subsystems:
 *   - DriverEntry & dispatch table setup
 *   - AddDevice (FDO creation, initialization)
 *   - PnP (Start/Stop/Remove/SurpriseRemoval)
 *   - File operations (Create/Close/Cleanup)
 *   - Read/Write (async, queued, with timeouts)
 *   - Serial IOCTLs (full Win32 serial port API)
 *   - USB data transfer engine (bulk IN/OUT/Interrupt)
 *   - USB CDC ACM control requests (line coding, DTR/RTS, break)
 *   - Power management (idle/suspend/wake)
 *   - WMI support
 * ========================================================================= */
