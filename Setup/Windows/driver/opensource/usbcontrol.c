/*
 * usbcontrol.c
 *
 * USB CDC ACM class-specific control requests sent over the default
 * control pipe: SET_LINE_CODING, GET_LINE_CODING, SET_CONTROL_LINE_STATE,
 * SEND_BREAK.
 *
 * SPDX-License-Identifier: MIT
 */

#include "mtk_usb2ser.h"

static
NTSTATUS
UsbControlSendClassRequest(
    _In_ PDEVICE_CONTEXT    DevCtx,
    _In_ UCHAR              Request,
    _In_ USHORT             Value,
    _In_opt_ PVOID          Buffer,
    _In_ ULONG              BufferLength,
    _In_ BOOLEAN            DirectionIn
    )
{
    NTSTATUS                    status;
    WDF_USB_CONTROL_SETUP_PACKET setupPacket;
    WDF_MEMORY_DESCRIPTOR       memDesc;
    WDFMEMORY                   wdfMemory = NULL;
    WDF_OBJECT_ATTRIBUTES       memAttrs;
    PVOID                       transferBuffer = NULL;

    RtlZeroMemory(&setupPacket, sizeof(setupPacket));

    setupPacket.Packet.bm.Request.Dir = DirectionIn ?
        BMREQUEST_DEVICE_TO_HOST : BMREQUEST_HOST_TO_DEVICE;
    setupPacket.Packet.bm.Request.Type = BMREQUEST_CLASS;
    setupPacket.Packet.bm.Request.Recipient = BMREQUEST_TO_INTERFACE;
    setupPacket.Packet.bRequest = Request;
    setupPacket.Packet.wValue.Value = Value;
    setupPacket.Packet.wIndex.Value = (USHORT)DevCtx->InterfaceNumber;
    setupPacket.Packet.wLength = (USHORT)BufferLength;

    if (BufferLength > 0) {
        WDF_OBJECT_ATTRIBUTES_INIT(&memAttrs);
        memAttrs.ParentObject = DevCtx->UsbDevice;

        status = WdfMemoryCreate(&memAttrs, NonPagedPoolNx, MTK_POOL_TAG,
                                 BufferLength, &wdfMemory, &transferBuffer);
        if (!NT_SUCCESS(status)) {
            return status;
        }

        if (!DirectionIn && Buffer != NULL) {
            RtlCopyMemory(transferBuffer, Buffer, BufferLength);
        }

        WDF_MEMORY_DESCRIPTOR_INIT_HANDLE(&memDesc, wdfMemory, NULL);

        status = WdfUsbTargetDeviceSendControlTransferSynchronously(
            DevCtx->UsbDevice,
            WDF_NO_HANDLE,
            NULL,
            &setupPacket,
            &memDesc,
            NULL
            );

        if (NT_SUCCESS(status) && DirectionIn && Buffer != NULL) {
            RtlCopyMemory(Buffer, transferBuffer, BufferLength);
        }

        WdfObjectDelete(wdfMemory);
    } else {
        status = WdfUsbTargetDeviceSendControlTransferSynchronously(
            DevCtx->UsbDevice,
            WDF_NO_HANDLE,
            NULL,
            &setupPacket,
            NULL,
            NULL
            );
    }

    return status;
}

NTSTATUS
UsbControlSetLineCoding(
    _In_ PDEVICE_CONTEXT DevCtx
    )
{
    return UsbControlSendClassRequest(
        DevCtx,
        CDC_SET_LINE_CODING,
        0,
        &DevCtx->LineCoding,
        sizeof(CDC_LINE_CODING),
        FALSE
        );
}

NTSTATUS
UsbControlGetLineCoding(
    _In_ PDEVICE_CONTEXT DevCtx
    )
{
    return UsbControlSendClassRequest(
        DevCtx,
        CDC_GET_LINE_CODING,
        0,
        &DevCtx->LineCoding,
        sizeof(CDC_LINE_CODING),
        TRUE
        );
}

NTSTATUS
UsbControlSetControlLineState(
    _In_ PDEVICE_CONTEXT DevCtx,
    _In_ USHORT          Value
    )
{
    return UsbControlSendClassRequest(
        DevCtx,
        CDC_SET_CONTROL_LINE_STATE,
        Value,
        NULL,
        0,
        FALSE
        );
}

NTSTATUS
UsbControlSendBreak(
    _In_ PDEVICE_CONTEXT DevCtx,
    _In_ USHORT          Duration
    )
{
    return UsbControlSendClassRequest(
        DevCtx,
        CDC_SEND_BREAK,
        Duration,
        NULL,
        0,
        FALSE
        );
}

NTSTATUS
UsbControlSetDtr(
    _In_ PDEVICE_CONTEXT DevCtx
    )
{
    USHORT value;

    DevCtx->DtrState = TRUE;

    value = 0;
    if (DevCtx->DtrState) value |= CDC_CTL_DTR;
    if (DevCtx->RtsState) value |= CDC_CTL_RTS;

    return UsbControlSetControlLineState(DevCtx, value);
}

NTSTATUS
UsbControlClrDtr(
    _In_ PDEVICE_CONTEXT DevCtx
    )
{
    USHORT value;

    DevCtx->DtrState = FALSE;

    value = 0;
    if (DevCtx->DtrState) value |= CDC_CTL_DTR;
    if (DevCtx->RtsState) value |= CDC_CTL_RTS;

    return UsbControlSetControlLineState(DevCtx, value);
}

NTSTATUS
UsbControlSetRts(
    _In_ PDEVICE_CONTEXT DevCtx
    )
{
    USHORT value;

    DevCtx->RtsState = TRUE;

    value = 0;
    if (DevCtx->DtrState) value |= CDC_CTL_DTR;
    if (DevCtx->RtsState) value |= CDC_CTL_RTS;

    return UsbControlSetControlLineState(DevCtx, value);
}

NTSTATUS
UsbControlClrRts(
    _In_ PDEVICE_CONTEXT DevCtx
    )
{
    USHORT value;

    DevCtx->RtsState = FALSE;

    value = 0;
    if (DevCtx->DtrState) value |= CDC_CTL_DTR;
    if (DevCtx->RtsState) value |= CDC_CTL_RTS;

    return UsbControlSetControlLineState(DevCtx, value);
}
