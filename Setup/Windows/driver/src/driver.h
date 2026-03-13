/*
 * driver.h - Main driver header for mtkclient KMDF USB driver
 *
 * Copyright (c) 2024-2025 mtkclient contributors
 * Licensed under GPLv3
 *
 * Internal declarations for the mtkclient WinUSB-compatible
 * KMDF function driver for MediaTek bootrom / preloader devices.
 */

#ifndef MTKCLIENT_DRIVER_H
#define MTKCLIENT_DRIVER_H

#include <ntddk.h>
#include <wdf.h>
#include <usb.h>
#include <usbdlib.h>
#include <wdfusb.h>
#include <initguid.h>

#include "public.h"

/* ================================================================
 * Driver version
 * ================================================================ */
#define MTKCLIENT_DRIVER_VERSION_MAJOR  1
#define MTKCLIENT_DRIVER_VERSION_MINOR  0
#define MTKCLIENT_DRIVER_VERSION_PATCH  0
#define MTKCLIENT_DRIVER_VERSION_BUILD  0

/* ================================================================
 * MediaTek USB IDs
 *
 * Sourced from: MediaTek SP Drivers 20160804 (cdc-acm.inf,
 * android_winusb.inf) and mtkclient usb_ids.py.
 * ================================================================ */
#define MTK_USB_VID             0x0E8D

/* Bootrom mode (BROM) – single-interface CDC */
#define MTK_PID_BROM            0x0003

/* Preloader VCOM – single-interface CDC */
#define MTK_PID_PRELOADER       0x2000

/* Download Agent (DA) VCOM */
#define MTK_PID_DA              0x2001

/* Generic Meta-mode VCOM */
#define MTK_PID_META            0x2007

/* mtkclient-specific preloader PIDs */
#define MTK_PID_PRELOADER_20FF  0x20FF
#define MTK_PID_PRELOADER_3000  0x3000
#define MTK_PID_PRELOADER_6000  0x6000

/* Android bootloader */
#define MTK_PID_BOOTLOADER      0x2024

/* Other vendors */
#define LG_USB_VID              0x1004
#define LG_PID_PRELOADER        0x6000
#define OPPO_USB_VID            0x22D9
#define OPPO_PID_PRELOADER      0x0006

/* ================================================================
 * CDC ACM class request codes
 * ================================================================ */
#define CDC_SEND_ENCAPSULATED_COMMAND   0x00
#define CDC_GET_ENCAPSULATED_RESPONSE   0x01
#define CDC_SET_LINE_CODING             0x20
#define CDC_GET_LINE_CODING             0x21
#define CDC_SET_CONTROL_LINE_STATE      0x22
#define CDC_SEND_BREAK                  0x23

/* bmRequestType values for CDC */
#define CDC_REQTYPE_HOST_TO_DEVICE  0x21  /* OUT | CLASS | INTERFACE */
#define CDC_REQTYPE_DEVICE_TO_HOST  0xA1  /* IN  | CLASS | INTERFACE */

/* ================================================================
 * Pool tags (for debug memory tracking)
 * ================================================================ */
#define MTKCLIENT_POOL_TAG  'CKTM'

/* ================================================================
 * Device context – stored per WDFDEVICE
 * ================================================================ */
typedef struct _DEVICE_CONTEXT {
    /* WDF USB target objects */
    WDFUSBDEVICE            UsbDevice;
    WDFUSBINTERFACE         UsbInterface;

    /* Bulk endpoints */
    WDFUSBPIPE              BulkReadPipe;
    WDFUSBPIPE              BulkWritePipe;

    /* Optional interrupt endpoint (CDC notification) */
    WDFUSBPIPE              InterruptPipe;

    /* Cached USB device descriptor */
    USB_DEVICE_DESCRIPTOR   DeviceDescriptor;

    /* Current interface number */
    UCHAR                   InterfaceNumber;

    /* Number of configured interfaces */
    UCHAR                   NumInterfaces;

    /* Connection state */
    BOOLEAN                 Connected;

    /* Current CDC line coding (7 bytes) */
    MTKCLIENT_LINE_CODING   LineCoding;

    /* Current control line state (DTR/RTS) */
    USHORT                  ControlLineState;

} DEVICE_CONTEXT, *PDEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, DeviceGetContext)

/* ================================================================
 * Forward declarations – driver.c
 * ================================================================ */
DRIVER_INITIALIZE  DriverEntry;

EVT_WDF_DRIVER_DEVICE_ADD           MtkEvtDeviceAdd;
EVT_WDF_DRIVER_UNLOAD               MtkEvtDriverUnload;
EVT_WDF_OBJECT_CONTEXT_CLEANUP      MtkEvtDriverContextCleanup;

/* ================================================================
 * Forward declarations – device.c
 * ================================================================ */
EVT_WDF_DEVICE_PREPARE_HARDWARE     MtkEvtDevicePrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE     MtkEvtDeviceReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY             MtkEvtDeviceD0Entry;
EVT_WDF_DEVICE_D0_EXIT              MtkEvtDeviceD0Exit;

NTSTATUS
MtkDeviceCreateUsbDevice(
    _In_ WDFDEVICE Device
    );

NTSTATUS
MtkDeviceSelectConfiguration(
    _In_ WDFDEVICE Device
    );

NTSTATUS
MtkDeviceConfigurePipes(
    _In_ WDFDEVICE Device
    );

/* ================================================================
 * Forward declarations – queue.c
 * ================================================================ */
NTSTATUS
MtkQueueInitialize(
    _In_ WDFDEVICE Device
    );

EVT_WDF_IO_QUEUE_IO_READ            MtkEvtIoRead;
EVT_WDF_IO_QUEUE_IO_WRITE           MtkEvtIoWrite;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL  MtkEvtIoDeviceControl;
EVT_WDF_IO_QUEUE_IO_STOP            MtkEvtIoStop;

/* ================================================================
 * Forward declarations – mtk_usb.c
 * ================================================================ */
NTSTATUS
MtkUsbSetLineCoding(
    _In_ PDEVICE_CONTEXT    DevCtx,
    _In_ PMTKCLIENT_LINE_CODING LineCoding
    );

NTSTATUS
MtkUsbGetLineCoding(
    _In_  PDEVICE_CONTEXT   DevCtx,
    _Out_ PMTKCLIENT_LINE_CODING LineCoding
    );

NTSTATUS
MtkUsbSetControlLineState(
    _In_ PDEVICE_CONTEXT    DevCtx,
    _In_ USHORT             ControlSignals
    );

NTSTATUS
MtkUsbSendBreak(
    _In_ PDEVICE_CONTEXT    DevCtx,
    _In_ USHORT             Duration
    );

NTSTATUS
MtkUsbVendorControlTransfer(
    _In_    PDEVICE_CONTEXT DevCtx,
    _Inout_ PMTKCLIENT_CTRL_TRANSFER Xfer,
    _Out_   PULONG BytesTransferred
    );

NTSTATUS
MtkUsbResetDevice(
    _In_ PDEVICE_CONTEXT    DevCtx
    );

NTSTATUS
MtkUsbBulkRead(
    _In_  PDEVICE_CONTEXT   DevCtx,
    _In_  WDFREQUEST        Request,
    _In_  size_t            Length
    );

NTSTATUS
MtkUsbBulkWrite(
    _In_  PDEVICE_CONTEXT   DevCtx,
    _In_  WDFREQUEST        Request,
    _In_  size_t            Length
    );

#endif /* MTKCLIENT_DRIVER_H */
