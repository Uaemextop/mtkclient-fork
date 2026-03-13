/*
 * serial.c — MediaTek CDC/ACM Serial KMDF Driver — CDC Class Requests
 *
 * Implements the four CDC Abstract Control Model class-specific
 * requests that control the virtual serial port:
 *
 *   SET_LINE_CODING          (0x20)  — baud, parity, stop, data bits
 *   GET_LINE_CODING          (0x21)  — read-back current settings
 *   SET_CONTROL_LINE_STATE   (0x22)  — DTR / RTS activation
 *   SEND_BREAK               (0x23)  — RS-232 break signal
 *
 * All requests are sent synchronously on the default control pipe
 * using WdfUsbTargetDeviceSendControlTransferSynchronously.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2024 mtkclient contributors
 */

#include "mtk_serial.h"

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, MtkSerialSetLineCoding)
#pragma alloc_text(PAGE, MtkSerialGetLineCoding)
#pragma alloc_text(PAGE, MtkSerialSetControlLineState)
#pragma alloc_text(PAGE, MtkSerialSendBreak)
#endif

/* ----------------------------------------------------------------
 * MtkSerialSetLineCoding — CDC SET_LINE_CODING (bRequest 0x20)
 *
 * Host → Device, class / interface, 7-byte payload.
 * ---------------------------------------------------------------- */
NTSTATUS
MtkSerialSetLineCoding(
    _In_ WDFDEVICE          Device,
    _In_ PCDC_LINE_CODING   LineCoding
    )
{
    PSERIAL_DEVICE_CONTEXT          pCtx;
    WDF_USB_CONTROL_SETUP_PACKET    setup;
    WDF_MEMORY_DESCRIPTOR           memDesc;
    NTSTATUS                        status;
    CDC_LINE_CODING                 buf;

    PAGED_CODE();

    pCtx = SerialGetDeviceContext(Device);
    if (pCtx->UsbDevice == NULL) {
        return STATUS_INVALID_DEVICE_STATE;
    }

    /* Copy to a local because the WDF call needs a non-const buffer */
    RtlCopyMemory(&buf, LineCoding, sizeof(buf));

    WDF_USB_CONTROL_SETUP_PACKET_INIT_CLASS(
        &setup,
        BmRequestHostToDevice,          /* Direction  */
        BmRequestToInterface,           /* Recipient  */
        CDC_REQUEST_SET_LINE_CODING,    /* bRequest   */
        0,                              /* wValue     */
        0);                             /* wIndex = interface 0 */

    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&memDesc, &buf, sizeof(buf));

    status = WdfUsbTargetDeviceSendControlTransferSynchronously(
        pCtx->UsbDevice,
        WDF_NO_HANDLE,          /* no WDFREQUEST — fire-and-forget */
        NULL,                   /* send options (defaults) */
        &setup,
        &memDesc,
        NULL);                  /* bytes transferred (don't need) */

    return status;
}

/* ----------------------------------------------------------------
 * MtkSerialGetLineCoding — CDC GET_LINE_CODING (bRequest 0x21)
 *
 * Device → Host, class / interface, 7-byte payload.
 * ---------------------------------------------------------------- */
NTSTATUS
MtkSerialGetLineCoding(
    _In_  WDFDEVICE         Device,
    _Out_ PCDC_LINE_CODING  LineCoding
    )
{
    PSERIAL_DEVICE_CONTEXT          pCtx;
    WDF_USB_CONTROL_SETUP_PACKET    setup;
    WDF_MEMORY_DESCRIPTOR           memDesc;
    ULONG                           bytesRead = 0;
    NTSTATUS                        status;
    CDC_LINE_CODING                 buf;

    PAGED_CODE();

    pCtx = SerialGetDeviceContext(Device);
    if (pCtx->UsbDevice == NULL) {
        return STATUS_INVALID_DEVICE_STATE;
    }

    RtlZeroMemory(&buf, sizeof(buf));

    WDF_USB_CONTROL_SETUP_PACKET_INIT_CLASS(
        &setup,
        BmRequestDeviceToHost,
        BmRequestToInterface,
        CDC_REQUEST_GET_LINE_CODING,
        0,
        0);

    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&memDesc, &buf, sizeof(buf));

    status = WdfUsbTargetDeviceSendControlTransferSynchronously(
        pCtx->UsbDevice,
        WDF_NO_HANDLE,
        NULL,
        &setup,
        &memDesc,
        &bytesRead);

    if (NT_SUCCESS(status) && bytesRead >= sizeof(CDC_LINE_CODING)) {
        RtlCopyMemory(LineCoding, &buf, sizeof(CDC_LINE_CODING));
    }

    return status;
}

/* ----------------------------------------------------------------
 * MtkSerialSetControlLineState — CDC SET_CONTROL_LINE_STATE (0x22)
 *
 * Host → Device, no data phase.
 *   wValue bit 0 = DTR
 *   wValue bit 1 = RTS
 * ---------------------------------------------------------------- */
NTSTATUS
MtkSerialSetControlLineState(
    _In_ WDFDEVICE  Device,
    _In_ USHORT     ControlSignals
    )
{
    PSERIAL_DEVICE_CONTEXT          pCtx;
    WDF_USB_CONTROL_SETUP_PACKET    setup;
    NTSTATUS                        status;

    PAGED_CODE();

    pCtx = SerialGetDeviceContext(Device);
    if (pCtx->UsbDevice == NULL) {
        return STATUS_INVALID_DEVICE_STATE;
    }

    WDF_USB_CONTROL_SETUP_PACKET_INIT_CLASS(
        &setup,
        BmRequestHostToDevice,
        BmRequestToInterface,
        CDC_REQUEST_SET_CONTROL_LINE_STATE,
        ControlSignals,                     /* wValue = DTR|RTS */
        0);                                 /* wIndex = interface 0 */

    status = WdfUsbTargetDeviceSendControlTransferSynchronously(
        pCtx->UsbDevice,
        WDF_NO_HANDLE,
        NULL,
        &setup,
        NULL,       /* no data phase */
        NULL);

    return status;
}

/* ----------------------------------------------------------------
 * MtkSerialSendBreak — CDC SEND_BREAK (bRequest 0x23)
 *
 * Host → Device, no data phase.
 *   wValue = duration in milliseconds (0xFFFF = until cleared)
 * ---------------------------------------------------------------- */
NTSTATUS
MtkSerialSendBreak(
    _In_ WDFDEVICE  Device,
    _In_ USHORT     Duration
    )
{
    PSERIAL_DEVICE_CONTEXT          pCtx;
    WDF_USB_CONTROL_SETUP_PACKET    setup;
    NTSTATUS                        status;

    PAGED_CODE();

    pCtx = SerialGetDeviceContext(Device);
    if (pCtx->UsbDevice == NULL) {
        return STATUS_INVALID_DEVICE_STATE;
    }

    WDF_USB_CONTROL_SETUP_PACKET_INIT_CLASS(
        &setup,
        BmRequestHostToDevice,
        BmRequestToInterface,
        CDC_REQUEST_SEND_BREAK,
        Duration,
        0);

    status = WdfUsbTargetDeviceSendControlTransferSynchronously(
        pCtx->UsbDevice,
        WDF_NO_HANDLE,
        NULL,
        &setup,
        NULL,
        NULL);

    return status;
}
