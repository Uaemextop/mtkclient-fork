/*
 * mtk_usb2ser.h
 *
 * Main header for the MediaTek USB CDC ACM to Serial Port KMDF driver.
 * Open-source reimplementation based on Capstone disassembly analysis
 * of the proprietary usb2ser.sys driver.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <ntddk.h>
#include <wdf.h>
#include <usb.h>
#include <usbdlib.h>
#include <wdfusb.h>
#include <initguid.h>
#include <devpkey.h>
#include <ntddser.h>
#include <ntstrsafe.h>

/* =========================================================================
 *  Modem Status Register (MSR) bit definitions — standard 8250 UART values.
 *  These may be missing from ntddser.h in WDK 10.0.26100+; provide fallbacks.
 * ========================================================================= */
#ifndef SERIAL_MSR_DCTS
#define SERIAL_MSR_DCTS     0x01
#endif
#ifndef SERIAL_MSR_DDSR
#define SERIAL_MSR_DDSR     0x02
#endif
#ifndef SERIAL_MSR_TERI
#define SERIAL_MSR_TERI     0x04
#endif
#ifndef SERIAL_MSR_DDCD
#define SERIAL_MSR_DDCD     0x08
#endif
#ifndef SERIAL_MSR_CTS
#define SERIAL_MSR_CTS      0x10
#endif
#ifndef SERIAL_MSR_DSR
#define SERIAL_MSR_DSR      0x20
#endif
#ifndef SERIAL_MSR_RI
#define SERIAL_MSR_RI       0x40
#endif
#ifndef SERIAL_MSR_DCD
#define SERIAL_MSR_DCD      0x80
#endif

/* =========================================================================
 *  Pool Tags (from disassembly: 'STCE' = 0x45435453, 'DSMU' for URBs)
 * ========================================================================= */
#define MTK_POOL_TAG        'KTMU'
#define MTK_POOL_TAG_URB    'DSMU'

/* =========================================================================
 *  USB CDC ACM Class-Specific Request Codes
 * ========================================================================= */
#define CDC_SET_LINE_CODING         0x20
#define CDC_GET_LINE_CODING         0x21
#define CDC_SET_CONTROL_LINE_STATE  0x22
#define CDC_SEND_BREAK              0x23

/* CDC ACM control line state bits (wValue of SET_CONTROL_LINE_STATE) */
#define CDC_CTL_DTR                 0x01
#define CDC_CTL_RTS                 0x02

/* CDC serial state notification bits (from interrupt endpoint) */
#define CDC_STATE_DCD               0x01
#define CDC_STATE_DSR               0x02
#define CDC_STATE_BREAK             0x04
#define CDC_STATE_RI                0x08
#define CDC_STATE_FRAMING           0x10
#define CDC_STATE_PARITY            0x20
#define CDC_STATE_OVERRUN           0x40

/* CDC notification codes */
#define CDC_NOTIF_SERIAL_STATE      0x20

/* =========================================================================
 *  Default Configuration Values (from registry / disassembly)
 * ========================================================================= */
#define DEFAULT_BAUD_RATE           115200
#define DEFAULT_DATA_BITS           8
#define DEFAULT_READ_BUFFER_SIZE    (64 * 1024)     /* 64 KB ring buffer */
#define DEFAULT_WRITE_BUFFER_SIZE   (64 * 1024)
#define MAX_TRANSFER_SIZE           4096
#define NUM_CONTINUOUS_READERS      8               /* Multiple outstanding IN URBs */
#define INTERRUPT_BUFFER_SIZE       16
#define DEFAULT_IDLE_TIME_SEC       10
#define DEFAULT_IN_QUEUE_SIZE       4096
#define DEFAULT_OUT_QUEUE_SIZE      4096

/* Max baud rate supported */
#define MAX_BAUD_RATE               921600

/* =========================================================================
 *  GUIDs
 * ========================================================================= */

/* GUID_DEVINTERFACE_COMPORT {86E0D1E0-8089-11D0-9CE4-08003E301F73} */
DEFINE_GUID(GUID_MTK_DEVINTERFACE_COMPORT,
    0x86E0D1E0, 0x8089, 0x11D0,
    0x9C, 0xE4, 0x08, 0x00, 0x3E, 0x30, 0x1F, 0x73);

/* Serial Port WMI GUIDs */
DEFINE_GUID(MTK_SERIAL_PORT_WMI_NAME_GUID,
    0xA0EC11A8, 0xB16C, 0x11D1,
    0xBD, 0x98, 0x00, 0xA0, 0xC9, 0x06, 0xBE, 0x2D);

DEFINE_GUID(MTK_SERIAL_PORT_WMI_COMM_GUID,
    0xEDB16A62, 0xB16C, 0x11D1,
    0xBD, 0x98, 0x00, 0xA0, 0xC9, 0x06, 0xBE, 0x2D);

DEFINE_GUID(MTK_SERIAL_PORT_WMI_HW_GUID,
    0x270B9B86, 0xB16D, 0x11D1,
    0xBD, 0x98, 0x00, 0xA0, 0xC9, 0x06, 0xBE, 0x2D);

DEFINE_GUID(MTK_SERIAL_PORT_WMI_PERF_GUID,
    0x56415ACC, 0xB16D, 0x11D1,
    0xBD, 0x98, 0x00, 0xA0, 0xC9, 0x06, 0xBE, 0x2D);

/* =========================================================================
 *  CDC Line Coding Structure (7 bytes, per USB CDC PSTN spec)
 * ========================================================================= */
#pragma pack(push, 1)
typedef struct _CDC_LINE_CODING {
    ULONG   dwDTERate;          /* Baud rate in bits per second */
    UCHAR   bCharFormat;        /* Stop bits: 0=1, 1=1.5, 2=2 */
    UCHAR   bParityType;        /* 0=None, 1=Odd, 2=Even, 3=Mark, 4=Space */
    UCHAR   bDataBits;          /* Number of data bits: 5, 6, 7, 8, 16 */
} CDC_LINE_CODING, *PCDC_LINE_CODING;
#pragma pack(pop)

/* =========================================================================
 *  Ring Buffer (from decompiled BUFFER_MANAGER)
 * ========================================================================= */
typedef struct _RING_BUFFER {
    PUCHAR      Buffer;             /* Circular buffer memory */
    ULONG       Size;               /* Total buffer capacity */
    ULONG       ReadOffset;         /* Current read position */
    ULONG       WriteOffset;        /* Current write position */
    ULONG       DataLength;         /* Bytes available to read */
    WDFSPINLOCK Lock;               /* Spin lock for concurrent access */
} RING_BUFFER, *PRING_BUFFER;

/* =========================================================================
 *  Device Context — main per-device data structure
 *  (KMDF equivalent of the WDM DEVICE_EXTENSION)
 * ========================================================================= */
typedef struct _DEVICE_CONTEXT {
    /* ---- WDF Handles ---- */
    WDFDEVICE           Device;
    WDFUSBDEVICE        UsbDevice;
    WDFUSBINTERFACE     UsbInterface;

    /* ---- USB Pipes ---- */
    WDFUSBPIPE          BulkInPipe;
    WDFUSBPIPE          BulkOutPipe;
    WDFUSBPIPE          InterruptPipe;
    ULONG               BulkInMaxPacket;
    ULONG               BulkOutMaxPacket;

    /* USB interface number (wIndex for CDC control requests) */
    UCHAR               InterfaceNumber;

    /*
     * USB serial number string (iSerialNumber descriptor).
     * Used to provide stable COM port assignment across PID changes
     * (e.g. Preloader PID_2000 → DA PID_2001 mode switch).
     * The same physical device always gets the same COMx port.
     */
    WCHAR               UsbSerialBuf[64];
    USHORT              UsbSerialLen;           /* bytes, 0 = not available */

    /* ---- Ring Buffer for Incoming Data ---- */
    RING_BUFFER         ReadBuffer;

    /* ---- I/O Queues ---- */
    WDFQUEUE            DefaultQueue;           /* Parallel: dispatches R/W/IOCTL */
    WDFQUEUE            PendingReadQueue;       /* Manual: reads waiting for data */
    WDFQUEUE            PendingWaitMaskQueue;   /* Manual: pending WAIT_ON_MASK */

    /* ---- Serial Port State ---- */
    CDC_LINE_CODING     LineCoding;
    SERIAL_HANDFLOW     HandFlow;
    SERIAL_TIMEOUTS     Timeouts;
    SERIAL_CHARS        SpecialChars;
    BOOLEAN             DtrState;
    BOOLEAN             RtsState;
    ULONG               ModemStatus;
    UCHAR               LsrMstInsert;       /* Escape char for LSR/MSR insertion */
    BOOLEAN             LsrMstInsertEnabled;

    /* ---- Wait Mask / Event Notification ---- */
    ULONG               WaitMask;
    ULONG               EventHistory;
    WDFSPINLOCK         EventLock;

    /* ---- Queue Sizes ---- */
    ULONG               InQueueSize;
    ULONG               OutQueueSize;

    /* ---- Performance Statistics ---- */
    SERIALPERF_STATS    PerfStats;

    /* ---- COM Port Symbolic Link ---- */
    WCHAR               PortNameBuf[32];    /* e.g. "COM3" */
    UNICODE_STRING      PortName;
    WCHAR               DeviceNameBuf[64];  /* e.g. "\\Device\\mtkcdcacm0" */
    UNICODE_STRING      DeviceName;
    WCHAR               DosNameBuf[64];     /* e.g. "\\DosDevices\\COM3" */
    UNICODE_STRING      DosName;
    LONG                PortIndex;
    BOOLEAN             SymbolicLinkCreated;
    BOOLEAN             SerialCommWritten;

    /* ---- Device Lifecycle State ---- */
    BOOLEAN             DeviceOpen;
    BOOLEAN             DeviceStarted;

    /* ---- Write Tracking ---- */
    WDFSPINLOCK         WriteLock;
    LONG                OutstandingWrites;

    /* ---- ZLP (Zero Length Packet) Support ---- */
    WDFWORKITEM         ZlpWorkItem;
    WDFREQUEST          PendingZlpCompleteRequest;
    ULONG_PTR           ZlpBytesWritten;

    /* ---- Idle / Power Settings (from registry) ---- */
    ULONG               IdleTimeSeconds;
    BOOLEAN             IdleEnabled;
    BOOLEAN             IdleWWBound;

    /* ---- Read Interval Timeout Timer ---- */
    /*
     * When SERIAL_TIMEOUTS.ReadIntervalTimeout is non-zero and non-MAXULONG,
     * a timer fires ReadIntervalTimeout ms after the last byte was received.
     * If there are pending read requests and data in the ring buffer, they
     * are completed immediately by the timer callback; if the ring buffer
     * still has no data the pending reads are completed with whatever is
     * available (possibly 0 bytes) to satisfy the Win32 timeout contract.
     *
     * This is required for mtkclient: pyserial sets ReadIntervalTimeout to
     * a finite value (20 ms default) and expects reads to return promptly
     * once the inter-character gap is detected.
     */
    WDFTIMER            ReadIntervalTimer;
    WDFSPINLOCK         ReadTimerLock;
    BOOLEAN             ReadTimerArmed;

} DEVICE_CONTEXT, *PDEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, GetDeviceContext)

/* =========================================================================
 *  Global State
 * ========================================================================= */
extern LONG g_DeviceCount;

/* =========================================================================
 *  Function Prototypes — driver.c
 * ========================================================================= */
DRIVER_INITIALIZE               DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD       EvtDriverDeviceAdd;
EVT_WDF_OBJECT_CONTEXT_CLEANUP  EvtDriverCleanup;

/* =========================================================================
 *  Function Prototypes — device.c
 * ========================================================================= */
EVT_WDF_DEVICE_PREPARE_HARDWARE     EvtDevicePrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE     EvtDeviceReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY             EvtDeviceD0Entry;
EVT_WDF_DEVICE_D0_EXIT              EvtDeviceD0Exit;
EVT_WDF_DEVICE_SURPRISE_REMOVAL     EvtDeviceSurpriseRemoval;
EVT_WDF_DEVICE_FILE_CREATE           EvtDeviceFileCreate;
EVT_WDF_FILE_CLOSE                   EvtFileClose;
EVT_WDF_FILE_CLEANUP                 EvtFileCleanup;

NTSTATUS    DeviceConfigureUsbDevice(_In_ PDEVICE_CONTEXT DevCtx);
NTSTATUS    DeviceConfigureUsbPipes(_In_ PDEVICE_CONTEXT DevCtx);
NTSTATUS    DeviceCreateSymbolicLink(_In_ PDEVICE_CONTEXT DevCtx);
VOID        DeviceRemoveSymbolicLink(_In_ PDEVICE_CONTEXT DevCtx);
NTSTATUS    DeviceReadRegistrySettings(_In_ PDEVICE_CONTEXT DevCtx);

/* =========================================================================
 *  Function Prototypes — queue.c
 * ========================================================================= */
NTSTATUS    QueueInitialize(_In_ WDFDEVICE Device);

EVT_WDF_IO_QUEUE_IO_READ               EvtIoRead;
EVT_WDF_IO_QUEUE_IO_WRITE              EvtIoWrite;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL     EvtIoDeviceControl;
EVT_WDF_TIMER                          EvtReadIntervalTimer;
VOID    ReadIntervalTimerArm(_In_ PDEVICE_CONTEXT DevCtx);

/* =========================================================================
 *  Function Prototypes — serial.c
 * ========================================================================= */
VOID    SerialSetBaudRate(_In_ PDEVICE_CONTEXT DevCtx, _In_ WDFREQUEST Request);
VOID    SerialGetBaudRate(_In_ PDEVICE_CONTEXT DevCtx, _In_ WDFREQUEST Request);
VOID    SerialSetLineControl(_In_ PDEVICE_CONTEXT DevCtx, _In_ WDFREQUEST Request);
VOID    SerialGetLineControl(_In_ PDEVICE_CONTEXT DevCtx, _In_ WDFREQUEST Request);
VOID    SerialSetHandflow(_In_ PDEVICE_CONTEXT DevCtx, _In_ WDFREQUEST Request);
VOID    SerialGetHandflow(_In_ PDEVICE_CONTEXT DevCtx, _In_ WDFREQUEST Request);
VOID    SerialSetDtr(_In_ PDEVICE_CONTEXT DevCtx, _In_ WDFREQUEST Request);
VOID    SerialClrDtr(_In_ PDEVICE_CONTEXT DevCtx, _In_ WDFREQUEST Request);
VOID    SerialSetRts(_In_ PDEVICE_CONTEXT DevCtx, _In_ WDFREQUEST Request);
VOID    SerialClrRts(_In_ PDEVICE_CONTEXT DevCtx, _In_ WDFREQUEST Request);
VOID    SerialGetDtrRts(_In_ PDEVICE_CONTEXT DevCtx, _In_ WDFREQUEST Request);
VOID    SerialGetModemStatus(_In_ PDEVICE_CONTEXT DevCtx, _In_ WDFREQUEST Request);
VOID    SerialSetBreakOn(_In_ PDEVICE_CONTEXT DevCtx, _In_ WDFREQUEST Request);
VOID    SerialSetBreakOff(_In_ PDEVICE_CONTEXT DevCtx, _In_ WDFREQUEST Request);
VOID    SerialSetTimeouts(_In_ PDEVICE_CONTEXT DevCtx, _In_ WDFREQUEST Request);
VOID    SerialGetTimeouts(_In_ PDEVICE_CONTEXT DevCtx, _In_ WDFREQUEST Request);
VOID    SerialSetChars(_In_ PDEVICE_CONTEXT DevCtx, _In_ WDFREQUEST Request);
VOID    SerialGetChars(_In_ PDEVICE_CONTEXT DevCtx, _In_ WDFREQUEST Request);
VOID    SerialSetQueueSize(_In_ PDEVICE_CONTEXT DevCtx, _In_ WDFREQUEST Request);
VOID    SerialSetWaitMask(_In_ PDEVICE_CONTEXT DevCtx, _In_ WDFREQUEST Request);
VOID    SerialGetWaitMask(_In_ PDEVICE_CONTEXT DevCtx, _In_ WDFREQUEST Request);
VOID    SerialWaitOnMask(_In_ PDEVICE_CONTEXT DevCtx, _In_ WDFREQUEST Request);
VOID    SerialPurge(_In_ PDEVICE_CONTEXT DevCtx, _In_ WDFREQUEST Request);
VOID    SerialGetCommStatus(_In_ PDEVICE_CONTEXT DevCtx, _In_ WDFREQUEST Request);
VOID    SerialGetProperties(_In_ PDEVICE_CONTEXT DevCtx, _In_ WDFREQUEST Request);
VOID    SerialGetStats(_In_ PDEVICE_CONTEXT DevCtx, _In_ WDFREQUEST Request);
VOID    SerialClearStats(_In_ PDEVICE_CONTEXT DevCtx, _In_ WDFREQUEST Request);
VOID    SerialConfigSize(_In_ PDEVICE_CONTEXT DevCtx, _In_ WDFREQUEST Request);
VOID    SerialLsrMstInsert(_In_ PDEVICE_CONTEXT DevCtx, _In_ WDFREQUEST Request);
VOID    SerialCompleteWaitOnMask(_In_ PDEVICE_CONTEXT DevCtx, _In_ ULONG Events);

/* =========================================================================
 *  Function Prototypes — usbtransfer.c
 * ========================================================================= */
NTSTATUS    RingBufferInit(_In_ PRING_BUFFER Rb, _In_ ULONG Size);
VOID        RingBufferFree(_In_ PRING_BUFFER Rb);
ULONG       RingBufferWrite(_In_ PRING_BUFFER Rb, _In_reads_(Length) PUCHAR Data, _In_ ULONG Length);
ULONG       RingBufferRead(_In_ PRING_BUFFER Rb, _Out_writes_(Length) PUCHAR Data, _In_ ULONG Length);
VOID        RingBufferPurge(_In_ PRING_BUFFER Rb);
ULONG       RingBufferGetDataLength(_In_ PRING_BUFFER Rb);

EVT_WDF_USB_READER_COMPLETION_ROUTINE   EvtUsbBulkInReadComplete;
EVT_WDF_USB_READERS_FAILED              EvtUsbBulkInReadersFailed;
EVT_WDF_USB_READER_COMPLETION_ROUTINE   EvtUsbInterruptReadComplete;
EVT_WDF_USB_READERS_FAILED              EvtUsbInterruptReadersFailed;
EVT_WDF_REQUEST_COMPLETION_ROUTINE      EvtWriteRequestComplete;
EVT_WDF_WORKITEM                        EvtZlpWorkItem;

VOID    UsbTransferProcessPendingReads(_In_ PDEVICE_CONTEXT DevCtx);
VOID    UsbTransferSerialStateNotify(_In_ PDEVICE_CONTEXT DevCtx, _In_ USHORT UartState);

/* =========================================================================
 *  Function Prototypes — usbcontrol.c
 * ========================================================================= */
NTSTATUS    UsbControlSetLineCoding(_In_ PDEVICE_CONTEXT DevCtx);
NTSTATUS    UsbControlGetLineCoding(_In_ PDEVICE_CONTEXT DevCtx);
NTSTATUS    UsbControlSetControlLineState(_In_ PDEVICE_CONTEXT DevCtx, _In_ USHORT Value);
NTSTATUS    UsbControlSendBreak(_In_ PDEVICE_CONTEXT DevCtx, _In_ USHORT Duration);
NTSTATUS    UsbControlSetDtr(_In_ PDEVICE_CONTEXT DevCtx);
NTSTATUS    UsbControlClrDtr(_In_ PDEVICE_CONTEXT DevCtx);
NTSTATUS    UsbControlSetRts(_In_ PDEVICE_CONTEXT DevCtx);
NTSTATUS    UsbControlClrRts(_In_ PDEVICE_CONTEXT DevCtx);

/* =========================================================================
 *  Function Prototypes — power.c
 * ========================================================================= */
NTSTATUS    PowerConfigureIdleSettings(_In_ PDEVICE_CONTEXT DevCtx);

/* =========================================================================
 *  Function Prototypes — wmi.c
 * ========================================================================= */
NTSTATUS    WmiRegistration(_In_ WDFDEVICE Device);
