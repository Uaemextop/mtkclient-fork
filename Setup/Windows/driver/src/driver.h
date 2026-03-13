/*
 * driver.h - mtkclient KMDF WinUSB Function Driver
 * (c) 2024-2026 GPLv3 License
 *
 * Minimal KMDF function driver for MediaTek USB devices.
 * Loads WinUSB.sys as the function driver, enabling user-mode
 * USB I/O via the WinUSB API without UsbDk or libusb.
 *
 * Build requirements:
 *   - Visual Studio 2022
 *   - Windows Driver Kit (WDK) 10.0.22621.0+
 *   - KMDF 1.15+
 *
 * This driver does NOT perform any kernel-mode USB I/O itself.
 * All USB communication is handled in user-mode by mtk_usb_driver.dll
 * via the WinUSB API (WinUsb_ReadPipe/WritePipe/ControlTransfer).
 */

#ifndef _MTKCLIENT_DRIVER_H_
#define _MTKCLIENT_DRIVER_H_

#include <ntddk.h>
#include <wdf.h>
#include <usb.h>
#include <usbdlib.h>
#include <initguid.h>

/* mtkclient device interface GUID - matches INF DeviceInterfaceGUIDs */
DEFINE_GUID(GUID_DEVINTERFACE_MTKCLIENT,
    0x1D0C3B4F, 0x2E1A, 0x4A32,
    0x9C, 0x3F, 0x5D, 0x6B, 0x7E, 0x8F, 0x9A, 0x0B);

/* Driver/device context (minimal - WinUSB handles everything) */
typedef struct _DEVICE_CONTEXT {
    WDFUSBDEVICE    UsbDevice;
    WDFUSBINTERFACE UsbInterface;
    UCHAR           BulkInPipe;
    UCHAR           BulkOutPipe;
    USHORT          VendorId;
    USHORT          ProductId;
} DEVICE_CONTEXT, *PDEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, DeviceGetContext)

/* Forward declarations */
DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD MtkEvtDeviceAdd;
EVT_WDF_DEVICE_PREPARE_HARDWARE MtkEvtDevicePrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE MtkEvtDeviceReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY MtkEvtDeviceD0Entry;
EVT_WDF_DEVICE_D0_EXIT MtkEvtDeviceD0Exit;

#endif /* _MTKCLIENT_DRIVER_H_ */
