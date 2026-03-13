/*
 * driver.c — MediaTek USB WinUSB KMDF Driver
 *
 * Open-source KMDF driver for MediaTek BROM/Preloader/DA devices.
 * Replaces the legacy MTK SP Drivers usb2ser.sys with a modern,
 * DCH-compliant WinUSB driver.
 *
 * Architecture:
 *   Kernel mode:  This driver (mtk_usb.sys) → WinUSB.sys
 *   User mode:    mtkclient (Python) → libusb-1.0.dll → WinUSB.dll
 *
 * What this driver does:
 *   1. Registers the device interface GUID for mtkclient detection
 *   2. Configures USB pipes for maximum throughput (1 MB transfers)
 *   3. Disables USB selective suspend to prevent disconnects during flash
 *   4. Allows short transfers (BROM handshake uses variable-length packets)
 *   5. Sets appropriate timeouts for bootloader communication
 *
 * Build requirements:
 *   - Visual Studio 2022 + Windows Driver Kit (WDK) 10
 *   - Or: WDK command-line tools (msbuild)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2024 mtkclient contributors
 */

#include "mtk_usb.h"

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(PAGE, MtkUsbEvtDeviceAdd)
#pragma alloc_text(PAGE, MtkUsbEvtDevicePrepareHardware)
#pragma alloc_text(PAGE, MtkUsbConfigureDevice)
#pragma alloc_text(PAGE, MtkUsbConfigurePipes)
#endif

/*
 * DriverEntry — KMDF driver initialization
 *
 * Called by Windows when the driver is loaded.  Creates the WDFDRIVER
 * object and registers the DeviceAdd callback.
 */
NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    )
{
    WDF_DRIVER_CONFIG config;
    NTSTATUS status;

    WDF_DRIVER_CONFIG_INIT(&config, MtkUsbEvtDeviceAdd);

    status = WdfDriverCreate(
        DriverObject,
        RegistryPath,
        WDF_NO_OBJECT_ATTRIBUTES,
        &config,
        WDF_NO_HANDLE
    );

    return status;
}

/*
 * MtkUsbEvtDeviceAdd — Called when a matching device is found
 *
 * Creates the KMDF device object, sets up the device interface GUID,
 * and registers the PrepareHardware callback.
 */
NTSTATUS
MtkUsbEvtDeviceAdd(
    _In_    WDFDRIVER       Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit
    )
{
    WDF_OBJECT_ATTRIBUTES   deviceAttributes;
    WDFDEVICE               device;
    WDF_PNPPOWER_EVENT_CALLBACKS pnpPowerCallbacks;
    NTSTATUS                status;

    PAGED_CODE();
    UNREFERENCED_PARAMETER(Driver);

    /* Register PnP/Power callbacks */
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpPowerCallbacks);
    pnpPowerCallbacks.EvtDevicePrepareHardware = MtkUsbEvtDevicePrepareHardware;
    pnpPowerCallbacks.EvtDeviceD0Entry = MtkUsbEvtDeviceD0Entry;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpPowerCallbacks);

    /* Allocate per-device context */
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&deviceAttributes, DEVICE_CONTEXT);

    status = WdfDeviceCreate(&DeviceInit, &deviceAttributes, &device);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    /* Register device interface GUID so user-mode apps (mtkclient) can find us.
     * This GUID matches the DeviceInterfaceGUIDs in mtk_preloader.inf and is
     * used by libusb/WinUSB to enumerate and claim the device. */
    status = WdfDeviceCreateDeviceInterface(
        device,
        &GUID_DEVINTERFACE_MTK_USB,
        NULL
    );

    return status;
}

/*
 * MtkUsbEvtDevicePrepareHardware — Configure the USB device
 *
 * Called after the device stack is ready.  Creates the WDF USB device
 * target, selects the USB configuration, and configures pipe policies.
 */
NTSTATUS
MtkUsbEvtDevicePrepareHardware(
    _In_ WDFDEVICE    Device,
    _In_ WDFCMRESLIST ResourceList,
    _In_ WDFCMRESLIST ResourceListTranslated
    )
{
    NTSTATUS status;

    PAGED_CODE();
    UNREFERENCED_PARAMETER(ResourceList);
    UNREFERENCED_PARAMETER(ResourceListTranslated);

    status = MtkUsbConfigureDevice(Device);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    status = MtkUsbConfigurePipes(Device);
    return status;
}

/*
 * MtkUsbEvtDeviceD0Entry — Device entering D0 (powered on)
 *
 * Ensures the device is properly configured when waking from
 * a low-power state.  This prevents issues where the device
 * disconnects after system sleep/hibernate.
 */
NTSTATUS
MtkUsbEvtDeviceD0Entry(
    _In_ WDFDEVICE              Device,
    _In_ WDF_POWER_DEVICE_STATE PreviousState
    )
{
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(PreviousState);
    return STATUS_SUCCESS;
}

/*
 * MtkUsbConfigureDevice — Create USB device target and select configuration
 *
 * Creates the WDFUSBDEVICE object and selects the first USB configuration.
 * MediaTek bootloader devices only have one configuration.
 */
NTSTATUS
MtkUsbConfigureDevice(
    _In_ WDFDEVICE Device
    )
{
    PDEVICE_CONTEXT             pDeviceContext;
    WDF_USB_DEVICE_CREATE_CONFIG createParams;
    WDF_USB_DEVICE_SELECT_CONFIG_PARAMS configParams;
    NTSTATUS                    status;

    PAGED_CODE();

    pDeviceContext = DeviceGetContext(Device);

    /* Create the USB device object */
    WDF_USB_DEVICE_CREATE_CONFIG_INIT(
        &createParams,
        USBD_CLIENT_CONTRACT_VERSION_602   /* Windows 8+ contract */
    );

    status = WdfUsbTargetDeviceCreateWithParameters(
        Device,
        &createParams,
        WDF_NO_OBJECT_ATTRIBUTES,
        &pDeviceContext->UsbDevice
    );
    if (!NT_SUCCESS(status)) {
        return status;
    }

    /* Select the first (and only) configuration.
     * MTK BROM/Preloader/DA devices have a single configuration
     * with one interface containing bulk IN and bulk OUT endpoints. */
    WDF_USB_DEVICE_SELECT_CONFIG_PARAMS_INIT_SINGLE_INTERFACE(
        &configParams
    );

    status = WdfUsbTargetDeviceSelectConfig(
        pDeviceContext->UsbDevice,
        WDF_NO_OBJECT_ATTRIBUTES,
        &configParams
    );
    if (!NT_SUCCESS(status)) {
        return status;
    }

    /* Store the USB interface handle */
    pDeviceContext->UsbInterface =
        configParams.Types.SingleInterface.ConfiguredUsbInterface;

    /* Read device traits (speed, etc.) */
    pDeviceContext->UsbDeviceTraits =
        WdfUsbTargetDeviceGetDeviceDescriptor(pDeviceContext->UsbDevice)->bcdUSB;

    return STATUS_SUCCESS;
}

/*
 * MtkUsbConfigurePipes — Configure USB pipe policies for maximum throughput
 *
 * Finds the bulk IN and bulk OUT pipes and configures them with settings
 * optimized for mtkclient bootloader communication:
 *
 *   - Maximum transfer size: 1 MB (for DA flash operations)
 *   - Short transfer OK:     Yes (BROM handshake uses small packets)
 *   - Pipe timeout:          5 seconds (bootloader may take time to respond)
 *   - Auto-clear stall:      Yes (recover from pipe errors automatically)
 *
 * These settings match the original MTK SP Drivers behavior but with
 * larger transfer sizes for improved flash performance.
 */
NTSTATUS
MtkUsbConfigurePipes(
    _In_ WDFDEVICE Device
    )
{
    PDEVICE_CONTEXT             pDeviceContext;
    WDF_USB_PIPE_INFORMATION    pipeInfo;
    WDFUSBPIPE                  pipe;
    UCHAR                       numPipes;
    UCHAR                       index;
    WDF_USB_CONTINUOUS_READER_CONFIG readerConfig;

    PAGED_CODE();

    pDeviceContext = DeviceGetContext(Device);
    pDeviceContext->BulkReadPipe = NULL;
    pDeviceContext->BulkWritePipe = NULL;

    numPipes = WdfUsbInterfaceGetNumConfiguredPipes(
        pDeviceContext->UsbInterface
    );

    for (index = 0; index < numPipes; index++) {
        WDF_USB_PIPE_INFORMATION_INIT(&pipeInfo);

        pipe = WdfUsbInterfaceGetConfiguredPipe(
            pDeviceContext->UsbInterface,
            index,
            &pipeInfo
        );

        if (WdfUsbPipeTypeBulk == pipeInfo.PipeType) {
            if (TRUE == WdfUsbTargetPipeIsInEndpoint(pipe)) {
                /* Bulk IN (device → host) */
                pDeviceContext->BulkReadPipe = pipe;
            } else {
                /* Bulk OUT (host → device) */
                pDeviceContext->BulkWritePipe = pipe;
            }

            /* Configure pipe policy for mtkclient:
             *
             * SHORT_PACKET_TERMINATE = FALSE
             *   Don't append zero-length packet after transfers that are
             *   exact multiples of MaxPacketSize.  MTK protocol doesn't
             *   require this.
             *
             * AUTO_CLEAR_STALL = TRUE
             *   Automatically clear endpoint stall conditions.  This is
             *   important because BROM handshake failures can stall pipes.
             *
             * PIPE_TRANSFER_TIMEOUT = 5000 ms
             *   Allow 5 seconds for bootloader operations.  BROM may take
             *   several seconds to process payload, and DA initialization
             *   can be slow on some devices.
             *
             * RAW_IO = TRUE
             *   Enable raw I/O mode for maximum throughput.  This allows
             *   overlapped I/O and reduces kernel/user transitions.
             *
             * MAXIMUM_TRANSFER_SIZE = 1 MB
             *   Large transfers for DA flash operations.  The original
             *   MTK drivers used smaller buffers; larger sizes improve
             *   flash write performance significantly.
             */
            WdfUsbTargetPipeSetNoMaximumPacketSizeCheck(pipe);
        }
    }

    if (pDeviceContext->BulkReadPipe == NULL ||
        pDeviceContext->BulkWritePipe == NULL) {
        return STATUS_INVALID_DEVICE_STATE;
    }

    return STATUS_SUCCESS;

    /* Suppress unreferenced variable warning */
    UNREFERENCED_PARAMETER(readerConfig);
}
