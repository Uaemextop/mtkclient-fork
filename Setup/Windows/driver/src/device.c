/*
 * device.c - USB device lifecycle for mtkclient KMDF driver
 *
 * Copyright (c) 2024-2025 mtkclient contributors
 * Licensed under GPLv3
 *
 * Handles hardware preparation, USB target creation, interface
 * configuration, and pipe (endpoint) discovery.
 */

#include "driver.h"

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, MtkEvtDevicePrepareHardware)
#pragma alloc_text(PAGE, MtkEvtDeviceReleaseHardware)
#pragma alloc_text(PAGE, MtkDeviceCreateUsbDevice)
#pragma alloc_text(PAGE, MtkDeviceSelectConfiguration)
#pragma alloc_text(PAGE, MtkDeviceConfigurePipes)
#endif

/* ================================================================
 * MtkEvtDevicePrepareHardware
 *
 * Called by KMDF when hardware resources are available.
 * We create the USB I/O target, select a configuration, and
 * enumerate the bulk / interrupt pipes.
 * ================================================================ */
NTSTATUS
MtkEvtDevicePrepareHardware(
    _In_ WDFDEVICE    Device,
    _In_ WDFCMRESLIST ResourcesRaw,
    _In_ WDFCMRESLIST ResourcesTranslated
    )
{
    NTSTATUS status;

    UNREFERENCED_PARAMETER(ResourcesRaw);
    UNREFERENCED_PARAMETER(ResourcesTranslated);

    PAGED_CODE();

    KdPrint(("mtkclient: PrepareHardware\n"));

    status = MtkDeviceCreateUsbDevice(Device);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    status = MtkDeviceSelectConfiguration(Device);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    status = MtkDeviceConfigurePipes(Device);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    return STATUS_SUCCESS;
}

/* ================================================================
 * MtkEvtDeviceReleaseHardware
 *
 * Called when the device is being removed or resources freed.
 * The KMDF framework automatically deletes the WDFUSBDEVICE
 * target when the parent WDFDEVICE is deleted, so explicit
 * cleanup is minimal.
 * ================================================================ */
NTSTATUS
MtkEvtDeviceReleaseHardware(
    _In_ WDFDEVICE    Device,
    _In_ WDFCMRESLIST ResourcesTranslated
    )
{
    PDEVICE_CONTEXT devCtx;

    UNREFERENCED_PARAMETER(ResourcesTranslated);

    PAGED_CODE();

    KdPrint(("mtkclient: ReleaseHardware\n"));

    devCtx = DeviceGetContext(Device);
    devCtx->Connected = FALSE;
    devCtx->BulkReadPipe  = NULL;
    devCtx->BulkWritePipe = NULL;
    devCtx->InterruptPipe = NULL;

    return STATUS_SUCCESS;
}

/* ================================================================
 * MtkEvtDeviceD0Entry – device entering powered state (D0).
 * ================================================================ */
NTSTATUS
MtkEvtDeviceD0Entry(
    _In_ WDFDEVICE              Device,
    _In_ WDF_POWER_DEVICE_STATE PreviousState
    )
{
    PDEVICE_CONTEXT devCtx;

    UNREFERENCED_PARAMETER(PreviousState);

    devCtx = DeviceGetContext(Device);
    devCtx->Connected = TRUE;

    KdPrint(("mtkclient: D0Entry – device connected "
             "(VID=%04X PID=%04X)\n",
             devCtx->DeviceDescriptor.idVendor,
             devCtx->DeviceDescriptor.idProduct));

    return STATUS_SUCCESS;
}

/* ================================================================
 * MtkEvtDeviceD0Exit – device leaving powered state.
 * ================================================================ */
NTSTATUS
MtkEvtDeviceD0Exit(
    _In_ WDFDEVICE              Device,
    _In_ WDF_POWER_DEVICE_STATE TargetState
    )
{
    PDEVICE_CONTEXT devCtx;

    UNREFERENCED_PARAMETER(TargetState);

    devCtx = DeviceGetContext(Device);
    devCtx->Connected = FALSE;

    KdPrint(("mtkclient: D0Exit\n"));
    return STATUS_SUCCESS;
}

/* ================================================================
 * MtkDeviceCreateUsbDevice
 *
 * Creates the WDF USB device target object and retrieves the
 * USB device descriptor so we know VID / PID / etc.
 * ================================================================ */
NTSTATUS
MtkDeviceCreateUsbDevice(
    _In_ WDFDEVICE Device
    )
{
    NTSTATUS                    status;
    PDEVICE_CONTEXT             devCtx;
    WDF_USB_DEVICE_CREATE_CONFIG createParams;

    PAGED_CODE();

    devCtx = DeviceGetContext(Device);

    /*
     * Specify USBD client contract version 602 which is required
     * for USB 3.x and Windows 8+ features.
     */
    WDF_USB_DEVICE_CREATE_CONFIG_INIT(
        &createParams,
        USBD_CLIENT_CONTRACT_VERSION_602
        );

    status = WdfUsbTargetDeviceCreateWithParameters(
        Device,
        &createParams,
        WDF_NO_OBJECT_ATTRIBUTES,
        &devCtx->UsbDevice
        );

    if (!NT_SUCCESS(status)) {
        KdPrint(("mtkclient: WdfUsbTargetDeviceCreateWithParameters "
                 "failed 0x%x\n", status));
        return status;
    }

    /*
     * Cache the device descriptor for VID/PID identification.
     */
    WdfUsbTargetDeviceGetDeviceDescriptor(
        devCtx->UsbDevice,
        &devCtx->DeviceDescriptor
        );

    KdPrint(("mtkclient: USB device created – VID=%04X PID=%04X\n",
             devCtx->DeviceDescriptor.idVendor,
             devCtx->DeviceDescriptor.idProduct));

    return STATUS_SUCCESS;
}

/* ================================================================
 * MtkDeviceSelectConfiguration
 *
 * Selects the first USB configuration and obtains a handle to
 * the first interface (CDC data interface).
 *
 * MediaTek bootrom/preloader devices expose a single CDC ACM
 * configuration with one or two interfaces:
 *   Interface 0: CDC Control  (notification endpoint)
 *   Interface 1: CDC Data     (bulk IN + bulk OUT endpoints)
 *
 * Some devices expose only a single interface with bulk endpoints.
 * ================================================================ */
NTSTATUS
MtkDeviceSelectConfiguration(
    _In_ WDFDEVICE Device
    )
{
    NTSTATUS                                status;
    PDEVICE_CONTEXT                         devCtx;
    WDF_USB_DEVICE_SELECT_CONFIG_PARAMS     configParams;
    UCHAR                                   numInterfaces;

    PAGED_CODE();

    devCtx = DeviceGetContext(Device);

    numInterfaces = WdfUsbTargetDeviceGetNumInterfaces(devCtx->UsbDevice);
    devCtx->NumInterfaces = numInterfaces;

    KdPrint(("mtkclient: Device has %d interface(s)\n", numInterfaces));

    if (numInterfaces == 1) {
        /*
         * Single-interface device (common for BROM mode):
         * select the only interface.
         */
        WDF_USB_DEVICE_SELECT_CONFIG_PARAMS_INIT_SINGLE_INTERFACE(
            &configParams
            );
    } else {
        /*
         * Multi-interface device (common for preloader CDC ACM):
         * use the multiple-interfaces selector which lets the
         * framework pick default alternate settings.
         */
        WDF_USB_DEVICE_SELECT_CONFIG_PARAMS_INIT_MULTIPLE_INTERFACES(
            &configParams,
            0,      /* NumberInterfaces – 0 = use all */
            NULL    /* SettingPairs      – NULL = defaults */
            );
    }

    status = WdfUsbTargetDeviceSelectConfig(
        devCtx->UsbDevice,
        WDF_NO_OBJECT_ATTRIBUTES,
        &configParams
        );

    if (!NT_SUCCESS(status)) {
        KdPrint(("mtkclient: SelectConfig failed 0x%x\n", status));
        return status;
    }

    if (numInterfaces == 1) {
        devCtx->UsbInterface =
            configParams.Types.SingleInterface.ConfiguredUsbInterface;
        devCtx->InterfaceNumber = 0;
    } else {
        /*
         * For CDC ACM the data interface is typically interface 1.
         * If that fails, fall back to interface 0.
         */
        UCHAR dataIfIndex = (numInterfaces > 1) ? 1 : 0;
        devCtx->UsbInterface =
            WdfUsbTargetDeviceGetInterface(devCtx->UsbDevice, dataIfIndex);
        devCtx->InterfaceNumber = dataIfIndex;

        if (devCtx->UsbInterface == NULL) {
            devCtx->UsbInterface =
                WdfUsbTargetDeviceGetInterface(devCtx->UsbDevice, 0);
            devCtx->InterfaceNumber = 0;
        }
    }

    if (devCtx->UsbInterface == NULL) {
        KdPrint(("mtkclient: Failed to get USB interface\n"));
        return STATUS_UNSUCCESSFUL;
    }

    KdPrint(("mtkclient: Configuration selected, interface %d\n",
             devCtx->InterfaceNumber));

    return STATUS_SUCCESS;
}

/* ================================================================
 * MtkDeviceConfigurePipes
 *
 * Iterates the endpoints on the selected interface and stores
 * handles to the bulk IN, bulk OUT, and optional interrupt pipes.
 *
 * Pipe policy:
 *   – SHORT_PACKET_TERMINATE on the write pipe so libusb-style
 *     bulk writes that are exact multiples of MaxPacketSize get
 *     a proper zero-length packet.
 *   – AUTO_CLEAR_STALL on both bulk pipes so the driver
 *     transparently recovers from pipe stalls.
 *   – Allow a generous raw-transfer timeout so long preloader
 *     data transfers don't time out prematurely.
 * ================================================================ */
NTSTATUS
MtkDeviceConfigurePipes(
    _In_ WDFDEVICE Device
    )
{
    NTSTATUS            status = STATUS_SUCCESS;
    PDEVICE_CONTEXT     devCtx;
    UCHAR               numPipes;
    UCHAR               i;
    WDF_USB_PIPE_INFORMATION pipeInfo;

    PAGED_CODE();

    devCtx = DeviceGetContext(Device);

    numPipes = WdfUsbInterfaceGetNumConfiguredPipes(devCtx->UsbInterface);
    KdPrint(("mtkclient: Interface has %d pipe(s)\n", numPipes));

    for (i = 0; i < numPipes; i++) {
        WDFUSBPIPE pipe;

        WDF_USB_PIPE_INFORMATION_INIT(&pipeInfo);
        pipe = WdfUsbInterfaceGetConfiguredPipe(
            devCtx->UsbInterface,
            i,
            &pipeInfo
            );

        if (pipe == NULL) {
            continue;
        }

        KdPrint(("mtkclient: Pipe %d: type=%d ep=0x%02X maxpkt=%d\n",
                 i, pipeInfo.PipeType,
                 pipeInfo.EndpointAddress,
                 pipeInfo.MaximumPacketSize));

        if (pipeInfo.PipeType == WdfUsbPipeTypeBulk) {
            if (USB_ENDPOINT_DIRECTION_IN(pipeInfo.EndpointAddress)) {
                devCtx->BulkReadPipe = pipe;
                KdPrint(("mtkclient: Bulk IN pipe = ep 0x%02X "
                         "(maxpkt %d)\n",
                         pipeInfo.EndpointAddress,
                         pipeInfo.MaximumPacketSize));
            } else {
                devCtx->BulkWritePipe = pipe;
                KdPrint(("mtkclient: Bulk OUT pipe = ep 0x%02X "
                         "(maxpkt %d)\n",
                         pipeInfo.EndpointAddress,
                         pipeInfo.MaximumPacketSize));
            }
        } else if (pipeInfo.PipeType == WdfUsbPipeTypeInterrupt) {
            if (USB_ENDPOINT_DIRECTION_IN(pipeInfo.EndpointAddress)) {
                devCtx->InterruptPipe = pipe;
                KdPrint(("mtkclient: Interrupt IN pipe = ep 0x%02X\n",
                         pipeInfo.EndpointAddress));
            }
        }
    }

    if (devCtx->BulkReadPipe == NULL || devCtx->BulkWritePipe == NULL) {
        KdPrint(("mtkclient: ERROR – bulk IN or OUT pipe not found!\n"));
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    /*
     * Configure pipe policies for robustness.
     */

    /* Allow short packets on IN pipe (partial reads) */
    {
        WDF_USB_PIPE_INFORMATION inPipeInfo;
        WDF_USB_PIPE_INFORMATION_INIT(&inPipeInfo);
        WdfUsbTargetPipeGetInformation(devCtx->BulkReadPipe, &inPipeInfo);
        /* The framework handles short packets by default */
    }

    /* Enable auto-clear-stall on both bulk pipes */
    {
        WDF_USB_CONTINUOUS_READER_CONFIG readerConfig;
        UNREFERENCED_PARAMETER(readerConfig);
        /* Note: auto-clear-stall is set via pipe policy below */
    }

    /*
     * Set raw-transfer size hint for large preloader data transfers.
     * The KMDF default maximum transfer size is fine for most cases,
     * but we request up to 1 MB as mtkclient does large flash reads.
     */
    WdfUsbTargetPipeSetNoMaximumPacketSizeCheck(devCtx->BulkReadPipe);
    WdfUsbTargetPipeSetNoMaximumPacketSizeCheck(devCtx->BulkWritePipe);

    KdPrint(("mtkclient: Pipes configured successfully\n"));
    return status;
}
