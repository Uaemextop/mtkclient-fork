/*
 * mtk_usb.h — MediaTek USB WinUSB Driver header
 *
 * Open-source KMDF driver for MediaTek BROM/Preloader/DA devices.
 * Configures WinUSB as the function driver with optimized settings
 * for mtkclient bootloader communication.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2024 mtkclient contributors
 */

#ifndef MTK_USB_H
#define MTK_USB_H

#include <ntddk.h>
#include <wdf.h>
#include <usb.h>
#include <wdfusb.h>

/* Device interface GUID — must match mtk_preloader.inf and mtkclient Python code */
/* {1D0C3B4F-2E1A-4A32-9C3F-5D6B7E8F9A0B} */
DEFINE_GUID(GUID_DEVINTERFACE_MTK_USB,
    0x1D0C3B4F, 0x2E1A, 0x4A32, 0x9C, 0x3F, 0x5D, 0x6B, 0x7E, 0x8F, 0x9A, 0x0B);

/*
 * MediaTek device modes (VID 0x0E8D):
 *   BROM       PID 0x0003  — BootROM mode, initial handshake at 115200 bps
 *   Preloader  PID 0x2000  — Preloader mode (also 0x20FF, 0x3000, 0x6000)
 *   DA         PID 0x2001  — Download Agent mode, high-speed up to 921600 bps
 */
#define MTK_VID             0x0E8D
#define MTK_PID_BROM        0x0003
#define MTK_PID_PRELOADER   0x2000
#define MTK_PID_DA          0x2001

/* USB transfer configuration for mtkclient */
#define MTK_MAX_TRANSFER_SIZE       (1024 * 1024)   /* 1 MB max per transfer */
#define MTK_PIPE_TIMEOUT_MS         5000             /* 5 second pipe timeout */
#define MTK_SHORT_TRANSFER_OK       TRUE             /* Allow short transfers */

/* Per-device context stored by KMDF */
typedef struct _DEVICE_CONTEXT {
    WDFUSBDEVICE        UsbDevice;
    WDFUSBINTERFACE     UsbInterface;
    WDFUSBPIPE          BulkReadPipe;
    WDFUSBPIPE          BulkWritePipe;
    ULONG               UsbDeviceTraits;
} DEVICE_CONTEXT, *PDEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, DeviceGetContext)

/* Driver entry points */
DRIVER_INITIALIZE           DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD   MtkUsbEvtDeviceAdd;
EVT_WDF_DEVICE_PREPARE_HARDWARE MtkUsbEvtDevicePrepareHardware;
EVT_WDF_DEVICE_D0_ENTRY     MtkUsbEvtDeviceD0Entry;

/* Helper functions */
NTSTATUS MtkUsbConfigureDevice(_In_ WDFDEVICE Device);
NTSTATUS MtkUsbConfigurePipes(_In_ WDFDEVICE Device);

#endif /* MTK_USB_H */
