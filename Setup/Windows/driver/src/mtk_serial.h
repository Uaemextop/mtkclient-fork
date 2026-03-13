/*
 * mtk_serial.h — MediaTek CDC/ACM Serial Driver Header
 *
 * Clean-room KMDF implementation of a CDC/ACM serial port driver
 * for MediaTek BROM/Preloader/DA devices.
 * Replaces proprietary usb2ser.sys from MTK SP Drivers.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2024 mtkclient contributors
 */

#ifndef MTK_SERIAL_H
#define MTK_SERIAL_H

#include <ntddk.h>
#include <wdf.h>
#include <usb.h>
#include <wdfusb.h>
#include <usbdlib.h>
#include <ntddser.h>
#include <ntstrsafe.h>

/* ================================================================
 * Device Interface GUID
 * {E3A3CE1F-2C3E-4D7A-B0F4-1A6F5E7C9D0B}
 * ================================================================ */
DEFINE_GUID(GUID_DEVINTERFACE_MTK_SERIAL,
    0xE3A3CE1F, 0x2C3E, 0x4D7A,
    0xB0, 0xF4, 0x1A, 0x6F, 0x5E, 0x7C, 0x9D, 0x0B);

/* ================================================================
 * CDC/ACM Class Request Codes  (USB CDC PSTN Subclass 1.2)
 * ================================================================ */
#define CDC_REQUEST_SET_LINE_CODING         0x20
#define CDC_REQUEST_GET_LINE_CODING         0x21
#define CDC_REQUEST_SET_CONTROL_LINE_STATE  0x22
#define CDC_REQUEST_SEND_BREAK              0x23

/* CDC SET_CONTROL_LINE_STATE wValue bits */
#define CDC_CONTROL_LINE_DTR    0x0001
#define CDC_CONTROL_LINE_RTS    0x0002

/* CDC interface class / subclass identifiers */
#define CDC_COMM_INTERFACE_CLASS         0x02
#define CDC_ACM_INTERFACE_SUBCLASS       0x02
#define CDC_AT_COMMAND_PROTOCOL          0x01
#define CDC_DATA_INTERFACE_CLASS         0x0A

/* CDC Line Coding — bCharFormat */
#define CDC_CHAR_FORMAT_1_STOP      0
#define CDC_CHAR_FORMAT_1_5_STOP    1
#define CDC_CHAR_FORMAT_2_STOP      2

/* CDC Line Coding — bParityType */
#define CDC_PARITY_NONE     0
#define CDC_PARITY_ODD      1
#define CDC_PARITY_EVEN     2
#define CDC_PARITY_MARK     3
#define CDC_PARITY_SPACE    4

/* ================================================================
 * CDC Line Coding Structure  (7 bytes — USB CDC PSTN Table 17)
 * ================================================================ */
#include <pshpack1.h>
typedef struct _CDC_LINE_CODING {
    ULONG   dwDTERate;      /* Data terminal rate, bits/sec              */
    UCHAR   bCharFormat;    /* 0 = 1 stop, 1 = 1.5 stop, 2 = 2 stop    */
    UCHAR   bParityType;    /* 0 = None … 4 = Space                     */
    UCHAR   bDataBits;      /* 5, 6, 7, 8, or 16                        */
} CDC_LINE_CODING, *PCDC_LINE_CODING;
#include <poppack.h>

C_ASSERT(sizeof(CDC_LINE_CODING) == 7);

/* ================================================================
 * Default serial settings  (matches mtkclient BROM handshake)
 * ================================================================ */
#define MTK_DEFAULT_BAUD_RATE       115200
#define MTK_DEFAULT_DATA_BITS       8
#define MTK_DEFAULT_PARITY          CDC_PARITY_NONE
#define MTK_DEFAULT_STOP_BITS       CDC_CHAR_FORMAT_1_STOP
#define MTK_MAX_BAUD_RATE           921600      /* DA high-speed mode */

/* ================================================================
 * USB transfer configuration
 * ================================================================ */
#define MTK_MAX_TRANSFER_SIZE       (1024 * 1024)   /* 1 MB  */
#define MTK_PIPE_TIMEOUT_MS         5000             /* 5 sec */

/* ================================================================
 * COM port naming helpers
 * ================================================================ */
#define MTK_DEVICE_NAME_PREFIX      L"\\Device\\cdcacm"
#define MTK_DOSDEVICE_PREFIX        L"\\DosDevices\\COM"
#define MTK_SERIALCOMM_PATH \
    L"\\Registry\\Machine\\HARDWARE\\DEVICEMAP\\SERIALCOMM"

/* ================================================================
 * SERIAL_COMMPROP  (kernel-mode definition — matches user COMMPROP)
 * Only defined when the WDK headers do not provide it.
 * ================================================================ */
#ifndef _SERIAL_COMMPROP_DEFINED
#define _SERIAL_COMMPROP_DEFINED
typedef struct _SERIAL_COMMPROP {
    USHORT  wPacketLength;
    USHORT  wPacketVersion;
    ULONG   dwServiceMask;
    ULONG   dwReserved1;
    ULONG   dwMaxTxQueue;
    ULONG   dwMaxRxQueue;
    ULONG   dwMaxBaud;
    ULONG   dwProvSubType;
    ULONG   dwProvCapabilities;
    ULONG   dwSettableParams;
    ULONG   dwSettableBaud;
    USHORT  wSettableData;
    USHORT  wSettableStopParity;
    ULONG   dwCurrentTxQueue;
    ULONG   dwCurrentRxQueue;
    ULONG   dwProvSpec1;
    ULONG   dwProvSpec2;
    WCHAR   wcProvChar[1];
} SERIAL_COMMPROP, *PSERIAL_COMMPROP;
#endif /* _SERIAL_COMMPROP_DEFINED */

/* COMMPROP capability constants (guarded against prior definitions) */
#ifndef SP_SERIALCOMM
#define SP_SERIALCOMM           0x00000001
#endif
#ifndef PST_RS232
#define PST_RS232               0x00000001
#endif
#ifndef PCF_DTRDSR
#define PCF_DTRDSR              0x0001
#define PCF_RTSCTS              0x0002
#define PCF_SETXCHAR            0x0020
#define PCF_TOTALTIMEOUTS       0x0040
#define PCF_INTTIMEOUTS         0x0080
#define PCF_PARITY_CHECK        0x0008
#endif
#ifndef SP_PARITY
#define SP_PARITY               0x0001
#define SP_BAUD                 0x0002
#define SP_DATABITS             0x0004
#define SP_STOPBITS             0x0008
#define SP_HANDSHAKING          0x0010
#define SP_PARITY_CHECK         0x0020
#endif
#ifndef BAUD_115200
#define BAUD_075                0x00000001
#define BAUD_110                0x00000002
#define BAUD_150                0x00000008
#define BAUD_300                0x00000010
#define BAUD_600                0x00000020
#define BAUD_1200               0x00000040
#define BAUD_1800               0x00000080
#define BAUD_2400               0x00000100
#define BAUD_4800               0x00000200
#define BAUD_7200               0x00000400
#define BAUD_9600               0x00000800
#define BAUD_14400              0x00001000
#define BAUD_19200              0x00002000
#define BAUD_38400              0x00004000
#define BAUD_56K                0x00008000
#define BAUD_128K               0x00010000
#define BAUD_115200             0x00020000
#define BAUD_57600              0x00040000
#define BAUD_USER               0x10000000
#endif
#ifndef DATABITS_8
#define DATABITS_5              0x0001
#define DATABITS_6              0x0002
#define DATABITS_7              0x0004
#define DATABITS_8              0x0008
#define DATABITS_16             0x0010
#endif
#ifndef STOPBITS_10
#define STOPBITS_10             0x0001
#define STOPBITS_15             0x0002
#define STOPBITS_20             0x0004
#endif
#ifndef PARITY_NONE_FLAG
#define PARITY_NONE_FLAG        0x0100
#define PARITY_ODD_FLAG         0x0200
#define PARITY_EVEN_FLAG        0x0400
#define PARITY_MARK_FLAG        0x0800
#define PARITY_SPACE_FLAG       0x1000
#endif

/* ================================================================
 * Per-device context
 * ================================================================ */
typedef struct _SERIAL_DEVICE_CONTEXT {
    /* ---- USB handles ---- */
    WDFUSBDEVICE        UsbDevice;
    WDFUSBINTERFACE     UsbControlInterface;    /* CDC comm (0x02/0x02) */
    WDFUSBINTERFACE     UsbDataInterface;       /* CDC data (0x0A)      */
    WDFUSBPIPE          BulkReadPipe;
    WDFUSBPIPE          BulkWritePipe;
    WDFUSBPIPE          InterruptPipe;          /* optional             */

    /* ---- CDC / serial state ---- */
    CDC_LINE_CODING     LineCoding;
    USHORT              ControlLineState;
    BOOLEAN             DtrEnabled;
    BOOLEAN             RtsEnabled;

    /* ---- Serial parameters ---- */
    SERIAL_TIMEOUTS     Timeouts;
    SERIAL_HANDFLOW     HandFlow;
    SERIAL_CHARS        SpecialChars;
    ULONG               WaitMask;
    ULONG               ModemStatus;

    /* ---- Synchronisation ---- */
    WDFWAITLOCK         SerialLock;

    /* ---- Manual queue for WAIT_ON_MASK ---- */
    WDFQUEUE            WaitMaskQueue;

    /* ---- COM port registration ---- */
    WCHAR               ComPortName[32];
    WCHAR               DeviceNameBuf[64];
    WCHAR               DosDeviceNameBuf[64];
    UNICODE_STRING      DeviceName;
    UNICODE_STRING      DosDeviceName;
    BOOLEAN             ComPortRegistered;
    BOOLEAN             SymLinkCreated;
    LONG                PortNumber;
} SERIAL_DEVICE_CONTEXT, *PSERIAL_DEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(SERIAL_DEVICE_CONTEXT, SerialGetDeviceContext)

/* ================================================================
 * Function prototypes  — driver.c
 * ================================================================ */
DRIVER_INITIALIZE                   DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD           MtkSerialEvtDeviceAdd;
EVT_WDF_DEVICE_FILE_CREATE          MtkSerialEvtDeviceFileCreate;
EVT_WDF_FILE_CLOSE                  MtkSerialEvtFileClose;

/* ================================================================
 * Function prototypes  — device.c
 * ================================================================ */
EVT_WDF_DEVICE_PREPARE_HARDWARE     MtkSerialEvtDevicePrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE     MtkSerialEvtDeviceReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY             MtkSerialEvtDeviceD0Entry;
EVT_WDF_DEVICE_D0_EXIT              MtkSerialEvtDeviceD0Exit;

NTSTATUS MtkSerialSelectInterfaces(_In_ WDFDEVICE Device);
NTSTATUS MtkSerialConfigurePipes(_In_ WDFDEVICE Device);
NTSTATUS MtkSerialRegisterComPort(_In_ WDFDEVICE Device);
VOID     MtkSerialUnregisterComPort(_In_ WDFDEVICE Device);

/* ================================================================
 * Function prototypes  — serial.c
 * ================================================================ */
NTSTATUS MtkSerialSetLineCoding(
    _In_ WDFDEVICE Device,
    _In_ PCDC_LINE_CODING LineCoding);

NTSTATUS MtkSerialGetLineCoding(
    _In_  WDFDEVICE Device,
    _Out_ PCDC_LINE_CODING LineCoding);

NTSTATUS MtkSerialSetControlLineState(
    _In_ WDFDEVICE Device,
    _In_ USHORT ControlSignals);

NTSTATUS MtkSerialSendBreak(
    _In_ WDFDEVICE Device,
    _In_ USHORT Duration);

/* ================================================================
 * Function prototypes  — queue.c
 * ================================================================ */
EVT_WDF_IO_QUEUE_IO_READ            MtkSerialEvtIoRead;
EVT_WDF_IO_QUEUE_IO_WRITE           MtkSerialEvtIoWrite;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL  MtkSerialEvtIoDeviceControl;
EVT_WDF_IO_QUEUE_IO_STOP            MtkSerialEvtIoStop;

NTSTATUS MtkSerialCreateQueues(_In_ WDFDEVICE Device);

#endif /* MTK_SERIAL_H */
