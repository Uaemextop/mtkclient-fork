/*
 * mtk_usb.c - MediaTek-specific USB operations for mtkclient KMDF driver
 *
 * Copyright (c) 2024-2025 mtkclient contributors
 * Licensed under GPLv3
 *
 * Implements CDC ACM class requests (SET_LINE_CODING, GET_LINE_CODING,
 * SET_CONTROL_LINE_STATE, SEND_BREAK) plus arbitrary vendor/class
 * control transfers used by the kamakiri bootrom exploits.
 *
 * VID/PID values sourced from MediaTek SP Drivers 20160804 and
 * extended with mtkclient-specific identifiers for Preloader,
 * BROM, DA, and Meta mode.
 */

#include "driver.h"

/* ================================================================
 * MtkUsbSetLineCoding
 *
 * CDC ACM SET_LINE_CODING (bRequest 0x20):
 *   bmRequestType = 0x21 (OUT | CLASS | INTERFACE)
 *   wValue        = 0
 *   wIndex        = interface number
 *   wLength       = 7
 *   Data          = {BaudRate[4], StopBits, Parity, DataBits}
 * ================================================================ */
NTSTATUS
MtkUsbSetLineCoding(
    _In_ PDEVICE_CONTEXT        DevCtx,
    _In_ PMTKCLIENT_LINE_CODING LineCoding
    )
{
    NTSTATUS                    status;
    WDF_USB_CONTROL_SETUP_PACKET setupPacket;
    WDF_MEMORY_DESCRIPTOR       memDesc;
    UCHAR                       payload[7];
    ULONG                       bytesTransferred = 0;

    if (DevCtx->UsbDevice == NULL) {
        return STATUS_DEVICE_NOT_CONNECTED;
    }

    /* Build the 7-byte CDC line coding payload */
    payload[0] = (UCHAR)(LineCoding->BaudRate & 0xFF);
    payload[1] = (UCHAR)((LineCoding->BaudRate >> 8) & 0xFF);
    payload[2] = (UCHAR)((LineCoding->BaudRate >> 16) & 0xFF);
    payload[3] = (UCHAR)((LineCoding->BaudRate >> 24) & 0xFF);
    payload[4] = LineCoding->StopBits;
    payload[5] = LineCoding->Parity;
    payload[6] = LineCoding->DataBits;

    WDF_USB_CONTROL_SETUP_PACKET_INIT_CLASS(
        &setupPacket,
        BmRequestHostToDevice,
        BmRequestToInterface,
        CDC_SET_LINE_CODING,            /* bRequest = 0x20 */
        0,                              /* wValue   = 0    */
        DevCtx->InterfaceNumber         /* wIndex           */
        );

    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&memDesc, payload, sizeof(payload));

    status = WdfUsbTargetDeviceSendControlTransferSynchronously(
        DevCtx->UsbDevice,
        WDF_NO_HANDLE,
        NULL,
        &setupPacket,
        &memDesc,
        &bytesTransferred
        );

    if (NT_SUCCESS(status)) {
        /* Cache the new line coding locally */
        DevCtx->LineCoding = *LineCoding;
        KdPrint(("mtkclient: SET_LINE_CODING baud=%u stop=%u par=%u "
                 "data=%u\n",
                 LineCoding->BaudRate, LineCoding->StopBits,
                 LineCoding->Parity,   LineCoding->DataBits));
    } else {
        KdPrint(("mtkclient: SET_LINE_CODING failed 0x%x\n", status));
    }

    return status;
}

/* ================================================================
 * MtkUsbGetLineCoding
 *
 * CDC ACM GET_LINE_CODING (bRequest 0x21):
 *   bmRequestType = 0xA1 (IN | CLASS | INTERFACE)
 * ================================================================ */
NTSTATUS
MtkUsbGetLineCoding(
    _In_  PDEVICE_CONTEXT        DevCtx,
    _Out_ PMTKCLIENT_LINE_CODING LineCoding
    )
{
    NTSTATUS                    status;
    WDF_USB_CONTROL_SETUP_PACKET setupPacket;
    WDF_MEMORY_DESCRIPTOR       memDesc;
    UCHAR                       payload[7];
    ULONG                       bytesTransferred = 0;

    if (DevCtx->UsbDevice == NULL) {
        return STATUS_DEVICE_NOT_CONNECTED;
    }

    RtlZeroMemory(payload, sizeof(payload));

    WDF_USB_CONTROL_SETUP_PACKET_INIT_CLASS(
        &setupPacket,
        BmRequestDeviceToHost,
        BmRequestToInterface,
        CDC_GET_LINE_CODING,            /* bRequest = 0x21 */
        0,                              /* wValue   = 0    */
        DevCtx->InterfaceNumber         /* wIndex           */
        );

    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&memDesc, payload, sizeof(payload));

    status = WdfUsbTargetDeviceSendControlTransferSynchronously(
        DevCtx->UsbDevice,
        WDF_NO_HANDLE,
        NULL,
        &setupPacket,
        &memDesc,
        &bytesTransferred
        );

    if (NT_SUCCESS(status) && bytesTransferred >= 7) {
        LineCoding->BaudRate = (ULONG)payload[0]
                             | ((ULONG)payload[1] << 8)
                             | ((ULONG)payload[2] << 16)
                             | ((ULONG)payload[3] << 24);
        LineCoding->StopBits = payload[4];
        LineCoding->Parity   = payload[5];
        LineCoding->DataBits = payload[6];

        /* Update local cache */
        DevCtx->LineCoding = *LineCoding;

        KdPrint(("mtkclient: GET_LINE_CODING baud=%u stop=%u par=%u "
                 "data=%u\n",
                 LineCoding->BaudRate, LineCoding->StopBits,
                 LineCoding->Parity,   LineCoding->DataBits));
    } else {
        KdPrint(("mtkclient: GET_LINE_CODING failed 0x%x (xferred=%u)\n",
                 status, bytesTransferred));
    }

    return status;
}

/* ================================================================
 * MtkUsbSetControlLineState
 *
 * CDC ACM SET_CONTROL_LINE_STATE (bRequest 0x22):
 *   bmRequestType = 0x21 (OUT | CLASS | INTERFACE)
 *   wValue        = control signals (bit 0 = DTR, bit 1 = RTS)
 * ================================================================ */
NTSTATUS
MtkUsbSetControlLineState(
    _In_ PDEVICE_CONTEXT DevCtx,
    _In_ USHORT          ControlSignals
    )
{
    NTSTATUS                    status;
    WDF_USB_CONTROL_SETUP_PACKET setupPacket;
    ULONG                       bytesTransferred = 0;

    if (DevCtx->UsbDevice == NULL) {
        return STATUS_DEVICE_NOT_CONNECTED;
    }

    WDF_USB_CONTROL_SETUP_PACKET_INIT_CLASS(
        &setupPacket,
        BmRequestHostToDevice,
        BmRequestToInterface,
        CDC_SET_CONTROL_LINE_STATE,     /* bRequest = 0x22 */
        ControlSignals,                 /* wValue           */
        DevCtx->InterfaceNumber         /* wIndex           */
        );

    status = WdfUsbTargetDeviceSendControlTransferSynchronously(
        DevCtx->UsbDevice,
        WDF_NO_HANDLE,
        NULL,
        &setupPacket,
        NULL,
        &bytesTransferred
        );

    if (NT_SUCCESS(status)) {
        DevCtx->ControlLineState = ControlSignals;
        KdPrint(("mtkclient: SET_CONTROL_LINE_STATE DTR=%d RTS=%d\n",
                 (ControlSignals & 1), (ControlSignals >> 1) & 1));
    } else {
        KdPrint(("mtkclient: SET_CONTROL_LINE_STATE failed 0x%x\n", status));
    }

    return status;
}

/* ================================================================
 * MtkUsbSendBreak
 *
 * CDC ACM SEND_BREAK (bRequest 0x23):
 *   bmRequestType = 0x21 (OUT | CLASS | INTERFACE)
 *   wValue        = duration in ms (0 = default)
 * ================================================================ */
NTSTATUS
MtkUsbSendBreak(
    _In_ PDEVICE_CONTEXT DevCtx,
    _In_ USHORT          Duration
    )
{
    NTSTATUS                    status;
    WDF_USB_CONTROL_SETUP_PACKET setupPacket;
    ULONG                       bytesTransferred = 0;

    if (DevCtx->UsbDevice == NULL) {
        return STATUS_DEVICE_NOT_CONNECTED;
    }

    WDF_USB_CONTROL_SETUP_PACKET_INIT_CLASS(
        &setupPacket,
        BmRequestHostToDevice,
        BmRequestToInterface,
        CDC_SEND_BREAK,                 /* bRequest = 0x23 */
        Duration,                       /* wValue           */
        DevCtx->InterfaceNumber         /* wIndex           */
        );

    status = WdfUsbTargetDeviceSendControlTransferSynchronously(
        DevCtx->UsbDevice,
        WDF_NO_HANDLE,
        NULL,
        &setupPacket,
        NULL,
        &bytesTransferred
        );

    if (NT_SUCCESS(status)) {
        KdPrint(("mtkclient: SEND_BREAK duration=%u ms\n", Duration));
    } else {
        KdPrint(("mtkclient: SEND_BREAK failed 0x%x\n", status));
    }

    return status;
}

/* ================================================================
 * MtkUsbVendorControlTransfer
 *
 * Executes an arbitrary USB control transfer.  This is the
 * critical path for kamakiri bootrom exploits which abuse
 * SET_LINE_CODING and GET_DESCRIPTOR to inject payloads.
 *
 * The caller provides the full setup packet in the
 * MTKCLIENT_CTRL_TRANSFER structure.
 * ================================================================ */
NTSTATUS
MtkUsbVendorControlTransfer(
    _In_    PDEVICE_CONTEXT         DevCtx,
    _Inout_ PMTKCLIENT_CTRL_TRANSFER Xfer,
    _Out_   PULONG                  BytesTransferred
    )
{
    NTSTATUS                    status;
    WDF_USB_CONTROL_SETUP_PACKET setupPacket;
    WDF_MEMORY_DESCRIPTOR       memDesc;
    BOOLEAN                     isIn;

    *BytesTransferred = 0;

    if (DevCtx->UsbDevice == NULL) {
        return STATUS_DEVICE_NOT_CONNECTED;
    }

    if (Xfer->wLength > sizeof(Xfer->Data)) {
        return STATUS_INVALID_PARAMETER;
    }

    isIn = (Xfer->bmRequestType & 0x80) ? TRUE : FALSE;

    /*
     * Build the raw setup packet from caller-provided fields.
     * This supports all bmRequestType combinations:
     *   0x21 = OUT|CLASS|INTERFACE  (CDC SET commands)
     *   0xA1 = IN|CLASS|INTERFACE   (CDC GET commands)
     *   0x40 = OUT|VENDOR|DEVICE    (vendor commands)
     *   0xC0 = IN|VENDOR|DEVICE     (vendor commands)
     *   0x80 = IN|STANDARD|DEVICE   (GET_DESCRIPTOR for exploits)
     */
    RtlZeroMemory(&setupPacket, sizeof(setupPacket));
    setupPacket.Packet.bm.Request.Dir   = isIn ? BmRequestDeviceToHost
                                                : BmRequestHostToDevice;
    setupPacket.Packet.bm.Request.Type  = (Xfer->bmRequestType >> 5) & 0x03;
    setupPacket.Packet.bm.Request.Recipient = Xfer->bmRequestType & 0x1F;
    setupPacket.Packet.bRequest             = Xfer->bRequest;
    setupPacket.Packet.wValue.Value         = Xfer->wValue;
    setupPacket.Packet.wIndex.Value         = Xfer->wIndex;

    if (Xfer->wLength > 0) {
        WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(
            &memDesc,
            Xfer->Data,
            Xfer->wLength
            );

        status = WdfUsbTargetDeviceSendControlTransferSynchronously(
            DevCtx->UsbDevice,
            WDF_NO_HANDLE,
            NULL,
            &setupPacket,
            &memDesc,
            BytesTransferred
            );
    } else {
        status = WdfUsbTargetDeviceSendControlTransferSynchronously(
            DevCtx->UsbDevice,
            WDF_NO_HANDLE,
            NULL,
            &setupPacket,
            NULL,
            BytesTransferred
            );
    }

    KdPrint(("mtkclient: CtrlXfer type=0x%02X req=0x%02X "
             "val=0x%04X idx=0x%04X len=%u -> 0x%x (%u bytes)\n",
             Xfer->bmRequestType, Xfer->bRequest,
             Xfer->wValue, Xfer->wIndex, Xfer->wLength,
             status, *BytesTransferred));

    return status;
}

/* ================================================================
 * MtkUsbResetDevice
 *
 * Performs a USB port reset.  After the reset the device typically
 * re-enumerates (possibly with a different PID if it has switched
 * from Preloader to DA mode).
 * ================================================================ */
NTSTATUS
MtkUsbResetDevice(
    _In_ PDEVICE_CONTEXT DevCtx
    )
{
    NTSTATUS status;

    if (DevCtx->UsbDevice == NULL) {
        return STATUS_DEVICE_NOT_CONNECTED;
    }

    KdPrint(("mtkclient: Resetting USB device\n"));

    status = WdfUsbTargetDeviceResetPortSynchronously(DevCtx->UsbDevice);

    if (NT_SUCCESS(status)) {
        KdPrint(("mtkclient: USB reset completed\n"));
    } else {
        KdPrint(("mtkclient: USB reset failed 0x%x\n", status));
    }

    return status;
}

/* ================================================================
 * MtkUsbBulkRead
 *
 * Sends a USB bulk IN request.  The framework-managed request is
 * forwarded to the USB I/O target and completed asynchronously
 * when data arrives from the device.
 *
 * This is the primary data path for receiving responses from:
 *   - Bootrom handshake (0xA0/0x0A/0x50/0x05 responses)
 *   - Preloader commands
 *   - DA flash read data
 * ================================================================ */
NTSTATUS
MtkUsbBulkRead(
    _In_ PDEVICE_CONTEXT DevCtx,
    _In_ WDFREQUEST      Request,
    _In_ size_t          Length
    )
{
    NTSTATUS status;
    WDFMEMORY memory;

    UNREFERENCED_PARAMETER(Length);

    status = WdfRequestRetrieveOutputMemory(Request, &memory);
    if (!NT_SUCCESS(status)) {
        KdPrint(("mtkclient: BulkRead RetrieveOutputMemory failed 0x%x\n",
                 status));
        return status;
    }

    status = WdfUsbTargetPipeFormatRequestForRead(
        DevCtx->BulkReadPipe,
        Request,
        memory,
        NULL
        );

    if (!NT_SUCCESS(status)) {
        KdPrint(("mtkclient: BulkRead FormatRequest failed 0x%x\n", status));
        return status;
    }

    WdfRequestSetCompletionRoutine(Request, NULL, NULL);

    if (!WdfRequestSend(
            Request,
            WdfUsbTargetPipeGetIoTarget(DevCtx->BulkReadPipe),
            WDF_NO_SEND_OPTIONS))
    {
        status = WdfRequestGetStatus(Request);
        KdPrint(("mtkclient: BulkRead WdfRequestSend failed 0x%x\n", status));
    }

    return status;
}

/* ================================================================
 * MtkUsbBulkWrite
 *
 * Sends a USB bulk OUT request.  Used for:
 *   - Bootrom handshake bytes (0xA0, 0x0A, 0x50, 0x05)
 *   - Preloader / DA commands and flash data
 * ================================================================ */
NTSTATUS
MtkUsbBulkWrite(
    _In_ PDEVICE_CONTEXT DevCtx,
    _In_ WDFREQUEST      Request,
    _In_ size_t          Length
    )
{
    NTSTATUS status;
    WDFMEMORY memory;

    UNREFERENCED_PARAMETER(Length);

    status = WdfRequestRetrieveInputMemory(Request, &memory);
    if (!NT_SUCCESS(status)) {
        KdPrint(("mtkclient: BulkWrite RetrieveInputMemory failed 0x%x\n",
                 status));
        return status;
    }

    status = WdfUsbTargetPipeFormatRequestForWrite(
        DevCtx->BulkWritePipe,
        Request,
        memory,
        NULL
        );

    if (!NT_SUCCESS(status)) {
        KdPrint(("mtkclient: BulkWrite FormatRequest failed 0x%x\n", status));
        return status;
    }

    WdfRequestSetCompletionRoutine(Request, NULL, NULL);

    if (!WdfRequestSend(
            Request,
            WdfUsbTargetPipeGetIoTarget(DevCtx->BulkWritePipe),
            WDF_NO_SEND_OPTIONS))
    {
        status = WdfRequestGetStatus(Request);
        KdPrint(("mtkclient: BulkWrite WdfRequestSend failed 0x%x\n", status));
    }

    return status;
}
