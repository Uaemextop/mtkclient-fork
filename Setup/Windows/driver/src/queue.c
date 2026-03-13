/*
 * queue.c - I/O Queue handling for mtkclient KMDF USB driver
 *
 * Copyright (c) 2024-2025 mtkclient contributors
 * Licensed under GPLv3
 *
 * Creates and manages the default I/O queue.  Dispatches
 * ReadFile, WriteFile, and DeviceIoControl requests to the
 * appropriate USB bulk or control transfer routines.
 */

#include "driver.h"

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, MtkQueueInitialize)
#endif

/* ================================================================
 * MtkQueueInitialize
 *
 * Creates a default, power-managed, parallel I/O queue for the
 * device.  All IRP_MJ_READ, IRP_MJ_WRITE, and
 * IRP_MJ_DEVICE_CONTROL requests arrive here.
 * ================================================================ */
NTSTATUS
MtkQueueInitialize(
    _In_ WDFDEVICE Device
    )
{
    NTSTATUS            status;
    WDF_IO_QUEUE_CONFIG queueConfig;

    PAGED_CODE();

    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(
        &queueConfig,
        WdfIoQueueDispatchParallel
        );

    queueConfig.EvtIoRead           = MtkEvtIoRead;
    queueConfig.EvtIoWrite          = MtkEvtIoWrite;
    queueConfig.EvtIoDeviceControl  = MtkEvtIoDeviceControl;
    queueConfig.EvtIoStop           = MtkEvtIoStop;

    status = WdfIoQueueCreate(
        Device,
        &queueConfig,
        WDF_NO_OBJECT_ATTRIBUTES,
        WDF_NO_HANDLE
        );

    if (!NT_SUCCESS(status)) {
        KdPrint(("mtkclient: WdfIoQueueCreate failed 0x%x\n", status));
    }

    return status;
}

/* ================================================================
 * MtkEvtIoRead – handles ReadFile() from user mode.
 *
 * Forwards the request as a USB bulk IN transfer to the device.
 * This is the primary path for receiving data from the MediaTek
 * bootrom / preloader / DA.
 * ================================================================ */
VOID
MtkEvtIoRead(
    _In_ WDFQUEUE   Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t     Length
    )
{
    NTSTATUS        status;
    WDFDEVICE       device;
    PDEVICE_CONTEXT devCtx;

    device = WdfIoQueueGetDevice(Queue);
    devCtx = DeviceGetContext(device);

    if (!devCtx->Connected || devCtx->BulkReadPipe == NULL) {
        WdfRequestComplete(Request, STATUS_DEVICE_NOT_CONNECTED);
        return;
    }

    status = MtkUsbBulkRead(devCtx, Request, Length);

    if (!NT_SUCCESS(status)) {
        KdPrint(("mtkclient: BulkRead failed 0x%x\n", status));
        WdfRequestComplete(Request, status);
    }
    /* On success the request is completed asynchronously by the
     * USB target when the transfer finishes. */
}

/* ================================================================
 * MtkEvtIoWrite – handles WriteFile() from user mode.
 *
 * Forwards the request as a USB bulk OUT transfer to the device.
 * This is the primary path for sending commands and data to the
 * MediaTek bootrom / preloader / DA.
 * ================================================================ */
VOID
MtkEvtIoWrite(
    _In_ WDFQUEUE   Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t     Length
    )
{
    NTSTATUS        status;
    WDFDEVICE       device;
    PDEVICE_CONTEXT devCtx;

    device = WdfIoQueueGetDevice(Queue);
    devCtx = DeviceGetContext(device);

    if (!devCtx->Connected || devCtx->BulkWritePipe == NULL) {
        WdfRequestComplete(Request, STATUS_DEVICE_NOT_CONNECTED);
        return;
    }

    status = MtkUsbBulkWrite(devCtx, Request, Length);

    if (!NT_SUCCESS(status)) {
        KdPrint(("mtkclient: BulkWrite failed 0x%x\n", status));
        WdfRequestComplete(Request, status);
    }
}

/* ================================================================
 * MtkEvtIoDeviceControl – handles DeviceIoControl() from user mode.
 *
 * Dispatches the custom IOCTL codes defined in public.h to the
 * corresponding MTK USB helper functions in mtk_usb.c.
 * ================================================================ */
VOID
MtkEvtIoDeviceControl(
    _In_ WDFQUEUE   Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t     OutputBufferLength,
    _In_ size_t     InputBufferLength,
    _In_ ULONG      IoControlCode
    )
{
    NTSTATUS        status = STATUS_INVALID_DEVICE_REQUEST;
    WDFDEVICE       device;
    PDEVICE_CONTEXT devCtx;
    size_t          bytesReturned = 0;

    device = WdfIoQueueGetDevice(Queue);
    devCtx = DeviceGetContext(device);

    switch (IoControlCode) {

    /* ---- GET_DEVICE_INFO ---- */
    case IOCTL_MTKCLIENT_GET_DEVICE_INFO:
    {
        PMTKCLIENT_DEVICE_INFO info = NULL;

        if (OutputBufferLength < sizeof(MTKCLIENT_DEVICE_INFO)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }

        status = WdfRequestRetrieveOutputBuffer(
            Request,
            sizeof(MTKCLIENT_DEVICE_INFO),
            (PVOID*)&info,
            NULL
            );

        if (NT_SUCCESS(status) && info != NULL) {
            info->VendorId         = devCtx->DeviceDescriptor.idVendor;
            info->ProductId        = devCtx->DeviceDescriptor.idProduct;
            info->InterfaceNumber  = devCtx->InterfaceNumber;
            info->Connected        = devCtx->Connected ? 1 : 0;
            info->DriverVersion[0] = MTKCLIENT_DRIVER_VERSION_MAJOR;
            info->DriverVersion[1] = MTKCLIENT_DRIVER_VERSION_MINOR;
            info->DriverVersion[2] = MTKCLIENT_DRIVER_VERSION_PATCH;
            info->DriverVersion[3] = MTKCLIENT_DRIVER_VERSION_BUILD;

            /* Retrieve max packet sizes from pipe info */
            if (devCtx->BulkReadPipe) {
                WDF_USB_PIPE_INFORMATION pInfo;
                WDF_USB_PIPE_INFORMATION_INIT(&pInfo);
                WdfUsbTargetPipeGetInformation(devCtx->BulkReadPipe, &pInfo);
                info->MaxPacketSizeIn = (USHORT)pInfo.MaximumPacketSize;
            }
            if (devCtx->BulkWritePipe) {
                WDF_USB_PIPE_INFORMATION pInfo;
                WDF_USB_PIPE_INFORMATION_INIT(&pInfo);
                WdfUsbTargetPipeGetInformation(devCtx->BulkWritePipe, &pInfo);
                info->MaxPacketSizeOut = (USHORT)pInfo.MaximumPacketSize;
            }

            bytesReturned = sizeof(MTKCLIENT_DEVICE_INFO);
        }
        break;
    }

    /* ---- SET_LINE_CODING ---- */
    case IOCTL_MTKCLIENT_SET_LINE_CODING:
    {
        PMTKCLIENT_LINE_CODING lc = NULL;

        if (InputBufferLength < sizeof(MTKCLIENT_LINE_CODING)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }

        status = WdfRequestRetrieveInputBuffer(
            Request,
            sizeof(MTKCLIENT_LINE_CODING),
            (PVOID*)&lc,
            NULL
            );

        if (NT_SUCCESS(status) && lc != NULL) {
            status = MtkUsbSetLineCoding(devCtx, lc);
        }
        break;
    }

    /* ---- GET_LINE_CODING ---- */
    case IOCTL_MTKCLIENT_GET_LINE_CODING:
    {
        PMTKCLIENT_LINE_CODING lc = NULL;

        if (OutputBufferLength < sizeof(MTKCLIENT_LINE_CODING)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }

        status = WdfRequestRetrieveOutputBuffer(
            Request,
            sizeof(MTKCLIENT_LINE_CODING),
            (PVOID*)&lc,
            NULL
            );

        if (NT_SUCCESS(status) && lc != NULL) {
            status = MtkUsbGetLineCoding(devCtx, lc);
            if (NT_SUCCESS(status)) {
                bytesReturned = sizeof(MTKCLIENT_LINE_CODING);
            }
        }
        break;
    }

    /* ---- SET_CONTROL_LINE_STATE ---- */
    case IOCTL_MTKCLIENT_SET_CONTROL_LINE_STATE:
    {
        PUSHORT pState = NULL;

        if (InputBufferLength < sizeof(USHORT)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }

        status = WdfRequestRetrieveInputBuffer(
            Request,
            sizeof(USHORT),
            (PVOID*)&pState,
            NULL
            );

        if (NT_SUCCESS(status) && pState != NULL) {
            status = MtkUsbSetControlLineState(devCtx, *pState);
        }
        break;
    }

    /* ---- SEND_BREAK ---- */
    case IOCTL_MTKCLIENT_SEND_BREAK:
    {
        PUSHORT pDuration = NULL;

        if (InputBufferLength < sizeof(USHORT)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }

        status = WdfRequestRetrieveInputBuffer(
            Request,
            sizeof(USHORT),
            (PVOID*)&pDuration,
            NULL
            );

        if (NT_SUCCESS(status) && pDuration != NULL) {
            status = MtkUsbSendBreak(devCtx, *pDuration);
        }
        break;
    }

    /* ---- VENDOR_CTRL_TRANSFER ---- */
    case IOCTL_MTKCLIENT_VENDOR_CTRL_TRANSFER:
    {
        PMTKCLIENT_CTRL_TRANSFER xfer = NULL;
        ULONG transferred = 0;
        size_t minSize = FIELD_OFFSET(MTKCLIENT_CTRL_TRANSFER, Data);

        if (InputBufferLength < minSize) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }

        status = WdfRequestRetrieveInputBuffer(
            Request,
            minSize,
            (PVOID*)&xfer,
            NULL
            );

        if (NT_SUCCESS(status) && xfer != NULL) {
            /* Validate wLength does not exceed Data buffer */
            if (xfer->wLength > sizeof(xfer->Data)) {
                status = STATUS_INVALID_PARAMETER;
                break;
            }

            status = MtkUsbVendorControlTransfer(devCtx, xfer, &transferred);

            if (NT_SUCCESS(status)) {
                /*
                 * For IN transfers (bit 7 of bmRequestType set),
                 * copy response data back to the output buffer.
                 */
                if (xfer->bmRequestType & 0x80) {
                    PMTKCLIENT_CTRL_TRANSFER outXfer = NULL;
                    if (OutputBufferLength >= minSize + transferred) {
                        status = WdfRequestRetrieveOutputBuffer(
                            Request,
                            minSize + transferred,
                            (PVOID*)&outXfer,
                            NULL
                            );
                        if (NT_SUCCESS(status) && outXfer != NULL) {
                            RtlCopyMemory(outXfer, xfer,
                                          minSize + transferred);
                            bytesReturned = minSize + transferred;
                        }
                    }
                }
            }
        }
        break;
    }

    /* ---- RESET_DEVICE ---- */
    case IOCTL_MTKCLIENT_RESET_DEVICE:
    {
        status = MtkUsbResetDevice(devCtx);
        break;
    }

    /* ---- REENUMERATE ---- */
    case IOCTL_MTKCLIENT_REENUMERATE:
    {
        /*
         * Request the PnP manager to re-enumerate this device.
         * This is used after a device mode switch (preloader → DA).
         */
        WdfDeviceSetFailed(device, WdfDeviceFailedNoRestart);
        status = STATUS_SUCCESS;
        break;
    }

    /* ---- GET_PIPE_INFO ---- */
    case IOCTL_MTKCLIENT_GET_PIPE_INFO:
    {
        PMTKCLIENT_PIPE_INFO pi = NULL;

        if (OutputBufferLength < sizeof(MTKCLIENT_PIPE_INFO)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }

        status = WdfRequestRetrieveOutputBuffer(
            Request,
            sizeof(MTKCLIENT_PIPE_INFO),
            (PVOID*)&pi,
            NULL
            );

        if (NT_SUCCESS(status) && pi != NULL) {
            RtlZeroMemory(pi, sizeof(MTKCLIENT_PIPE_INFO));

            if (devCtx->BulkReadPipe) {
                WDF_USB_PIPE_INFORMATION pInfo;
                WDF_USB_PIPE_INFORMATION_INIT(&pInfo);
                WdfUsbTargetPipeGetInformation(devCtx->BulkReadPipe, &pInfo);
                pi->BulkInEndpoint  = pInfo.EndpointAddress;
                pi->BulkInMaxPacket = (USHORT)pInfo.MaximumPacketSize;
            }
            if (devCtx->BulkWritePipe) {
                WDF_USB_PIPE_INFORMATION pInfo;
                WDF_USB_PIPE_INFORMATION_INIT(&pInfo);
                WdfUsbTargetPipeGetInformation(devCtx->BulkWritePipe, &pInfo);
                pi->BulkOutEndpoint  = pInfo.EndpointAddress;
                pi->BulkOutMaxPacket = (USHORT)pInfo.MaximumPacketSize;
            }
            if (devCtx->InterruptPipe) {
                WDF_USB_PIPE_INFORMATION pInfo;
                WDF_USB_PIPE_INFORMATION_INIT(&pInfo);
                WdfUsbTargetPipeGetInformation(devCtx->InterruptPipe, &pInfo);
                pi->InterruptEndpoint  = pInfo.EndpointAddress;
                pi->InterruptMaxPacket = (USHORT)pInfo.MaximumPacketSize;
            }

            bytesReturned = sizeof(MTKCLIENT_PIPE_INFO);
        }
        break;
    }

    default:
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

    WdfRequestCompleteWithInformation(Request, status, bytesReturned);
}

/* ================================================================
 * MtkEvtIoStop – called when the I/O queue is stopping.
 *
 * Acknowledges the stop so the framework can drain the queue
 * cleanly during power-down or removal.
 * ================================================================ */
VOID
MtkEvtIoStop(
    _In_ WDFQUEUE   Queue,
    _In_ WDFREQUEST Request,
    _In_ ULONG      ActionFlags
    )
{
    UNREFERENCED_PARAMETER(Queue);

    if (ActionFlags & WdfRequestStopActionSuspend) {
        WdfRequestStopAcknowledge(Request, FALSE);
    } else if (ActionFlags & WdfRequestStopActionPurge) {
        WdfRequestCancelSentRequest(Request);
    }
}
