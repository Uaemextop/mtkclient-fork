/*
 * public.h - Public header for mtkclient KMDF USB driver
 *
 * Copyright (c) 2024-2025 mtkclient contributors
 * Licensed under GPLv3
 *
 * This header is shared between the kernel-mode driver and
 * user-mode applications.  It defines the device interface GUID
 * and all custom IOCTL codes.
 */

#ifndef MTKCLIENT_PUBLIC_H
#define MTKCLIENT_PUBLIC_H

/*
 * Device interface GUID – user-mode applications use this GUID
 * with SetupDiGetClassDevs / CM_Get_Device_Interface_List to
 * discover and open the mtkclient driver device.
 *
 * {13EB360B-BC1E-46CB-B606-B4884B5E8B23}
 */
DEFINE_GUID(GUID_DEVINTERFACE_MTKCLIENT,
    0x13eb360b, 0xbc1e, 0x46cb,
    0xb6, 0x06, 0xb4, 0x88, 0x4b, 0x5e, 0x8b, 0x23);

/* ================================================================
 * Custom IOCTL codes
 *
 * FILE_DEVICE_UNKNOWN = 0x22
 * We use function codes starting at 0x800 (user-defined range).
 * ================================================================ */

#define FILE_DEVICE_MTKCLIENT  0x8000

/*
 * IOCTL_MTKCLIENT_GET_DEVICE_INFO
 *   Retrieves the current device VID, PID, interface number,
 *   and connection status.
 *
 *   Input:  None
 *   Output: MTKCLIENT_DEVICE_INFO structure
 */
#define IOCTL_MTKCLIENT_GET_DEVICE_INFO \
    CTL_CODE(FILE_DEVICE_MTKCLIENT, 0x800, METHOD_BUFFERED, FILE_READ_ACCESS)

/*
 * IOCTL_MTKCLIENT_SET_LINE_CODING
 *   CDC ACM SET_LINE_CODING – sets baud rate, stop bits,
 *   parity, and data bits on the device.
 *
 *   Input:  MTKCLIENT_LINE_CODING structure (7 bytes packed)
 *   Output: None
 */
#define IOCTL_MTKCLIENT_SET_LINE_CODING \
    CTL_CODE(FILE_DEVICE_MTKCLIENT, 0x801, METHOD_BUFFERED, FILE_WRITE_ACCESS)

/*
 * IOCTL_MTKCLIENT_GET_LINE_CODING
 *   CDC ACM GET_LINE_CODING – reads current line coding.
 *
 *   Input:  None
 *   Output: MTKCLIENT_LINE_CODING structure
 */
#define IOCTL_MTKCLIENT_GET_LINE_CODING \
    CTL_CODE(FILE_DEVICE_MTKCLIENT, 0x802, METHOD_BUFFERED, FILE_READ_ACCESS)

/*
 * IOCTL_MTKCLIENT_SET_CONTROL_LINE_STATE
 *   CDC ACM SET_CONTROL_LINE_STATE – sets DTR / RTS.
 *
 *   Input:  USHORT (bit 0 = DTR, bit 1 = RTS)
 *   Output: None
 */
#define IOCTL_MTKCLIENT_SET_CONTROL_LINE_STATE \
    CTL_CODE(FILE_DEVICE_MTKCLIENT, 0x803, METHOD_BUFFERED, FILE_WRITE_ACCESS)

/*
 * IOCTL_MTKCLIENT_SEND_BREAK
 *   CDC ACM SEND_BREAK – sends a break signal.
 *
 *   Input:  USHORT wValue (break duration in ms, 0 = default)
 *   Output: None
 */
#define IOCTL_MTKCLIENT_SEND_BREAK \
    CTL_CODE(FILE_DEVICE_MTKCLIENT, 0x804, METHOD_BUFFERED, FILE_WRITE_ACCESS)

/*
 * IOCTL_MTKCLIENT_VENDOR_CTRL_TRANSFER
 *   Executes an arbitrary USB control transfer (vendor or class).
 *   Used by mtkclient exploit code (kamakiri) that needs raw
 *   control transfer access.
 *
 *   Input:  MTKCLIENT_CTRL_TRANSFER structure
 *   Output: Up to wLength bytes of data (for IN transfers)
 */
#define IOCTL_MTKCLIENT_VENDOR_CTRL_TRANSFER \
    CTL_CODE(FILE_DEVICE_MTKCLIENT, 0x805, METHOD_BUFFERED, FILE_ANY_ACCESS)

/*
 * IOCTL_MTKCLIENT_RESET_DEVICE
 *   Requests a USB port reset for the device.
 *
 *   Input:  None
 *   Output: None
 */
#define IOCTL_MTKCLIENT_RESET_DEVICE \
    CTL_CODE(FILE_DEVICE_MTKCLIENT, 0x806, METHOD_BUFFERED, FILE_WRITE_ACCESS)

/*
 * IOCTL_MTKCLIENT_REENUMERATE
 *   Requests a USB device re-enumeration.
 *
 *   Input:  None
 *   Output: None
 */
#define IOCTL_MTKCLIENT_REENUMERATE \
    CTL_CODE(FILE_DEVICE_MTKCLIENT, 0x807, METHOD_BUFFERED, FILE_WRITE_ACCESS)

/*
 * IOCTL_MTKCLIENT_GET_PIPE_INFO
 *   Returns bulk IN/OUT pipe configuration.
 *
 *   Input:  None
 *   Output: MTKCLIENT_PIPE_INFO structure
 */
#define IOCTL_MTKCLIENT_GET_PIPE_INFO \
    CTL_CODE(FILE_DEVICE_MTKCLIENT, 0x808, METHOD_BUFFERED, FILE_READ_ACCESS)

/* ================================================================
 * Data structures shared between kernel and user mode
 * ================================================================ */

#pragma pack(push, 1)

/*
 * Device information returned by IOCTL_MTKCLIENT_GET_DEVICE_INFO.
 */
typedef struct _MTKCLIENT_DEVICE_INFO {
    USHORT  VendorId;
    USHORT  ProductId;
    UCHAR   InterfaceNumber;
    UCHAR   Connected;
    UCHAR   DriverVersion[4];   /* Major.Minor.Patch.Build */
    USHORT  MaxPacketSizeIn;
    USHORT  MaxPacketSizeOut;
} MTKCLIENT_DEVICE_INFO, *PMTKCLIENT_DEVICE_INFO;

/*
 * CDC ACM line coding (7 bytes):
 *   dwDTERate     – baud rate (4 bytes, little-endian)
 *   bCharFormat   – stop bits: 0=1, 1=1.5, 2=2
 *   bParityType   – 0=None, 1=Odd, 2=Even, 3=Mark, 4=Space
 *   bDataBits     – 5, 6, 7, 8, or 16
 */
typedef struct _MTKCLIENT_LINE_CODING {
    ULONG   BaudRate;
    UCHAR   StopBits;
    UCHAR   Parity;
    UCHAR   DataBits;
} MTKCLIENT_LINE_CODING, *PMTKCLIENT_LINE_CODING;

/*
 * Arbitrary USB control transfer request.
 *   Used for kamakiri exploits and vendor-specific commands.
 */
typedef struct _MTKCLIENT_CTRL_TRANSFER {
    UCHAR   bmRequestType;
    UCHAR   bRequest;
    USHORT  wValue;
    USHORT  wIndex;
    USHORT  wLength;
    UCHAR   Data[4096];     /* Variable-length data payload */
} MTKCLIENT_CTRL_TRANSFER, *PMTKCLIENT_CTRL_TRANSFER;

/*
 * Pipe (endpoint) information.
 */
typedef struct _MTKCLIENT_PIPE_INFO {
    UCHAR   BulkInEndpoint;
    UCHAR   BulkOutEndpoint;
    USHORT  BulkInMaxPacket;
    USHORT  BulkOutMaxPacket;
    UCHAR   InterruptEndpoint;
    USHORT  InterruptMaxPacket;
} MTKCLIENT_PIPE_INFO, *PMTKCLIENT_PIPE_INFO;

#pragma pack(pop)

#endif /* MTKCLIENT_PUBLIC_H */
