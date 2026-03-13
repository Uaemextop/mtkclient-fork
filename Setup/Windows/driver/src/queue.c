/*
 * queue.c — MediaTek CDC/ACM Serial KMDF Driver — I/O Queue Handlers
 *
 * Three WDFQUEUE objects service all I/O:
 *
 *   1. Default sequential queue   — EvtIoRead / EvtIoWrite
 *   2. Parallel device-control    — EvtIoDeviceControl
 *   3. Manual wait-mask queue     — holds pending WAIT_ON_MASK requests
 *
 * The device-control handler implements the full set of serial IOCTLs
 * expected by Win32 applications that open COMx ports.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2024 mtkclient contributors
 */

#include "mtk_serial.h"

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, MtkSerialCreateQueues)
#pragma alloc_text(PAGE, MtkSerialEvtIoRead)
#pragma alloc_text(PAGE, MtkSerialEvtIoWrite)
#pragma alloc_text(PAGE, MtkSerialEvtIoDeviceControl)
#endif
/* NOTE: EvtIoStop must NOT be paged — may run at DISPATCH_LEVEL */

/* ================================================================
 * Queue creation
 * ================================================================ */
NTSTATUS
MtkSerialCreateQueues(
    _In_ WDFDEVICE Device
    )
{
    PSERIAL_DEVICE_CONTEXT  pCtx;
    WDF_IO_QUEUE_CONFIG     queueCfg;
    WDFQUEUE                queue;
    NTSTATUS                status;

    PAGED_CODE();

    pCtx = SerialGetDeviceContext(Device);

    /* ---- 1. Default sequential queue for Read / Write ---- */
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueCfg,
        WdfIoQueueDispatchSequential);
    queueCfg.EvtIoRead  = MtkSerialEvtIoRead;
    queueCfg.EvtIoWrite = MtkSerialEvtIoWrite;
    queueCfg.EvtIoStop  = MtkSerialEvtIoStop;

    status = WdfIoQueueCreate(Device, &queueCfg,
                              WDF_NO_OBJECT_ATTRIBUTES, &queue);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    /* ---- 2. Parallel queue for DeviceControl (IOCTLs) ---- */
    WDF_IO_QUEUE_CONFIG_INIT(&queueCfg, WdfIoQueueDispatchParallel);
    queueCfg.EvtIoDeviceControl = MtkSerialEvtIoDeviceControl;
    queueCfg.EvtIoStop          = MtkSerialEvtIoStop;

    status = WdfIoQueueCreate(Device, &queueCfg,
                              WDF_NO_OBJECT_ATTRIBUTES, &queue);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    status = WdfDeviceConfigureRequestDispatching(
        Device, queue, WdfRequestTypeDeviceControl);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    /* ---- 3. Manual queue for WAIT_ON_MASK notifications ---- */
    WDF_IO_QUEUE_CONFIG_INIT(&queueCfg, WdfIoQueueDispatchManual);

    status = WdfIoQueueCreate(Device, &queueCfg,
                              WDF_NO_OBJECT_ATTRIBUTES,
                              &pCtx->WaitMaskQueue);
    return status;
}

/* ================================================================
 * EvtIoRead — Bulk IN pipe → caller buffer
 * ================================================================ */
VOID
MtkSerialEvtIoRead(
    _In_ WDFQUEUE   Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t     Length
    )
{
    WDFDEVICE               device;
    PSERIAL_DEVICE_CONTEXT  pCtx;
    WDFMEMORY               mem;
    NTSTATUS                status;
    ULONG_PTR               bytesRead = 0;

    PAGED_CODE();

    device = WdfIoQueueGetDevice(Queue);
    pCtx   = SerialGetDeviceContext(device);

    if (pCtx->BulkReadPipe == NULL) {
        WdfRequestComplete(Request, STATUS_INVALID_DEVICE_STATE);
        return;
    }

    status = WdfRequestRetrieveOutputMemory(Request, &mem);
    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
        return;
    }

    status = WdfUsbTargetPipeReadSynchronously(
        pCtx->BulkReadPipe,
        Request,
        NULL,       /* pipe-read options */
        mem,
        NULL,       /* offsets (read full buffer) */
        &bytesRead);

    WdfRequestCompleteWithInformation(Request, status, bytesRead);
    UNREFERENCED_PARAMETER(Length);
}

/* ================================================================
 * EvtIoWrite — Caller buffer → Bulk OUT pipe
 * ================================================================ */
VOID
MtkSerialEvtIoWrite(
    _In_ WDFQUEUE   Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t     Length
    )
{
    WDFDEVICE               device;
    PSERIAL_DEVICE_CONTEXT  pCtx;
    WDFMEMORY               mem;
    NTSTATUS                status;
    ULONG_PTR               bytesWritten = 0;

    PAGED_CODE();

    device = WdfIoQueueGetDevice(Queue);
    pCtx   = SerialGetDeviceContext(device);

    if (pCtx->BulkWritePipe == NULL) {
        WdfRequestComplete(Request, STATUS_INVALID_DEVICE_STATE);
        return;
    }

    status = WdfRequestRetrieveInputMemory(Request, &mem);
    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
        return;
    }

    status = WdfUsbTargetPipeWriteSynchronously(
        pCtx->BulkWritePipe,
        Request,
        NULL,
        mem,
        NULL,
        &bytesWritten);

    WdfRequestCompleteWithInformation(Request, status, bytesWritten);
    UNREFERENCED_PARAMETER(Length);
}

/* ================================================================
 * EvtIoDeviceControl — Serial IOCTL dispatcher
 * ================================================================ */
VOID
MtkSerialEvtIoDeviceControl(
    _In_ WDFQUEUE   Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t     OutputBufferLength,
    _In_ size_t     InputBufferLength,
    _In_ ULONG      IoControlCode
    )
{
    WDFDEVICE               device;
    PSERIAL_DEVICE_CONTEXT  pCtx;
    NTSTATUS                status = STATUS_SUCCESS;
    ULONG_PTR               info   = 0;
    PVOID                   inBuf  = NULL;
    PVOID                   outBuf = NULL;

    PAGED_CODE();
    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);

    device = WdfIoQueueGetDevice(Queue);
    pCtx   = SerialGetDeviceContext(device);

    switch (IoControlCode) {

    /* ============================================================
     * Baud rate
     * ============================================================ */
    case IOCTL_SERIAL_SET_BAUD_RATE:
    {
        PSERIAL_BAUD_RATE pBaud;
        status = WdfRequestRetrieveInputBuffer(
            Request, sizeof(SERIAL_BAUD_RATE), &inBuf, NULL);
        if (!NT_SUCCESS(status)) break;

        pBaud = (PSERIAL_BAUD_RATE)inBuf;

        WdfWaitLockAcquire(pCtx->SerialLock, NULL);
        pCtx->LineCoding.dwDTERate = pBaud->BaudRate;
        status = MtkSerialSetLineCoding(device, &pCtx->LineCoding);
        WdfWaitLockRelease(pCtx->SerialLock);
        break;
    }

    case IOCTL_SERIAL_GET_BAUD_RATE:
    {
        PSERIAL_BAUD_RATE pBaud;
        status = WdfRequestRetrieveOutputBuffer(
            Request, sizeof(SERIAL_BAUD_RATE), &outBuf, NULL);
        if (!NT_SUCCESS(status)) break;

        pBaud = (PSERIAL_BAUD_RATE)outBuf;
        pBaud->BaudRate = pCtx->LineCoding.dwDTERate;
        info = sizeof(SERIAL_BAUD_RATE);
        break;
    }

    /* ============================================================
     * Line control (stop bits, parity, data bits)
     * ============================================================ */
    case IOCTL_SERIAL_SET_LINE_CONTROL:
    {
        PSERIAL_LINE_CONTROL pLine;
        status = WdfRequestRetrieveInputBuffer(
            Request, sizeof(SERIAL_LINE_CONTROL), &inBuf, NULL);
        if (!NT_SUCCESS(status)) break;

        pLine = (PSERIAL_LINE_CONTROL)inBuf;

        WdfWaitLockAcquire(pCtx->SerialLock, NULL);
        pCtx->LineCoding.bCharFormat = pLine->StopBits;
        pCtx->LineCoding.bParityType = pLine->Parity;
        pCtx->LineCoding.bDataBits   = pLine->WordLength;
        status = MtkSerialSetLineCoding(device, &pCtx->LineCoding);
        WdfWaitLockRelease(pCtx->SerialLock);
        break;
    }

    case IOCTL_SERIAL_GET_LINE_CONTROL:
    {
        PSERIAL_LINE_CONTROL pLine;
        status = WdfRequestRetrieveOutputBuffer(
            Request, sizeof(SERIAL_LINE_CONTROL), &outBuf, NULL);
        if (!NT_SUCCESS(status)) break;

        pLine = (PSERIAL_LINE_CONTROL)outBuf;
        pLine->StopBits   = pCtx->LineCoding.bCharFormat;
        pLine->Parity     = pCtx->LineCoding.bParityType;
        pLine->WordLength  = pCtx->LineCoding.bDataBits;
        info = sizeof(SERIAL_LINE_CONTROL);
        break;
    }

    /* ============================================================
     * DTR / RTS modem-control lines
     * ============================================================ */
    case IOCTL_SERIAL_SET_DTR:
    {
        WdfWaitLockAcquire(pCtx->SerialLock, NULL);
        pCtx->DtrEnabled = TRUE;
        pCtx->ControlLineState |= CDC_CONTROL_LINE_DTR;
        status = MtkSerialSetControlLineState(device, pCtx->ControlLineState);
        WdfWaitLockRelease(pCtx->SerialLock);
        break;
    }

    case IOCTL_SERIAL_CLR_DTR:
    {
        WdfWaitLockAcquire(pCtx->SerialLock, NULL);
        pCtx->DtrEnabled = FALSE;
        pCtx->ControlLineState &= ~CDC_CONTROL_LINE_DTR;
        status = MtkSerialSetControlLineState(device, pCtx->ControlLineState);
        WdfWaitLockRelease(pCtx->SerialLock);
        break;
    }

    case IOCTL_SERIAL_SET_RTS:
    {
        WdfWaitLockAcquire(pCtx->SerialLock, NULL);
        pCtx->RtsEnabled = TRUE;
        pCtx->ControlLineState |= CDC_CONTROL_LINE_RTS;
        status = MtkSerialSetControlLineState(device, pCtx->ControlLineState);
        WdfWaitLockRelease(pCtx->SerialLock);
        break;
    }

    case IOCTL_SERIAL_CLR_RTS:
    {
        WdfWaitLockAcquire(pCtx->SerialLock, NULL);
        pCtx->RtsEnabled = FALSE;
        pCtx->ControlLineState &= ~CDC_CONTROL_LINE_RTS;
        status = MtkSerialSetControlLineState(device, pCtx->ControlLineState);
        WdfWaitLockRelease(pCtx->SerialLock);
        break;
    }

    /* ============================================================
     * Break signal
     * ============================================================ */
    case IOCTL_SERIAL_SET_BREAK_ON:
        status = MtkSerialSendBreak(device, 0xFFFF);
        break;

    case IOCTL_SERIAL_SET_BREAK_OFF:
        status = MtkSerialSendBreak(device, 0);
        break;

    /* ============================================================
     * Communication status
     * ============================================================ */
    case IOCTL_SERIAL_GET_COMMSTATUS:
    {
        PSERIAL_STATUS pStat;
        status = WdfRequestRetrieveOutputBuffer(
            Request, sizeof(SERIAL_STATUS), &outBuf, NULL);
        if (!NT_SUCCESS(status)) break;

        pStat = (PSERIAL_STATUS)outBuf;
        RtlZeroMemory(pStat, sizeof(SERIAL_STATUS));
        info = sizeof(SERIAL_STATUS);
        break;
    }

    /* ============================================================
     * Communication properties / capabilities
     * ============================================================ */
    case IOCTL_SERIAL_GET_PROPERTIES:
    {
        PSERIAL_COMMPROP pProp;
        status = WdfRequestRetrieveOutputBuffer(
            Request, sizeof(SERIAL_COMMPROP), &outBuf, NULL);
        if (!NT_SUCCESS(status)) break;

        pProp = (PSERIAL_COMMPROP)outBuf;
        RtlZeroMemory(pProp, sizeof(SERIAL_COMMPROP));

        pProp->wPacketLength    = sizeof(SERIAL_COMMPROP);
        pProp->wPacketVersion   = 2;
        pProp->dwServiceMask    = SP_SERIALCOMM;
        pProp->dwMaxTxQueue     = 0;
        pProp->dwMaxRxQueue     = 0;
        pProp->dwMaxBaud        = MTK_MAX_BAUD_RATE;
        pProp->dwProvSubType    = PST_RS232;
        pProp->dwProvCapabilities =
            PCF_DTRDSR | PCF_RTSCTS | PCF_TOTALTIMEOUTS |
            PCF_INTTIMEOUTS | PCF_PARITY_CHECK | PCF_SETXCHAR;
        pProp->dwSettableParams =
            SP_PARITY | SP_BAUD | SP_DATABITS |
            SP_STOPBITS | SP_HANDSHAKING | SP_PARITY_CHECK;
        pProp->dwSettableBaud =
            BAUD_110 | BAUD_300 | BAUD_600 | BAUD_1200 |
            BAUD_2400 | BAUD_4800 | BAUD_9600 | BAUD_14400 |
            BAUD_19200 | BAUD_38400 | BAUD_57600 | BAUD_115200 |
            BAUD_USER;
        pProp->wSettableData =
            DATABITS_5 | DATABITS_6 | DATABITS_7 | DATABITS_8;
        pProp->wSettableStopParity =
            STOPBITS_10 | STOPBITS_15 | STOPBITS_20 |
            PARITY_NONE_FLAG | PARITY_ODD_FLAG |
            PARITY_EVEN_FLAG | PARITY_MARK_FLAG | PARITY_SPACE_FLAG;

        info = sizeof(SERIAL_COMMPROP);
        break;
    }

    /* ============================================================
     * Purge (cancel pending I/O)
     * ============================================================ */
    case IOCTL_SERIAL_PURGE:
    {
        PULONG pMask;
        status = WdfRequestRetrieveInputBuffer(
            Request, sizeof(ULONG), &inBuf, NULL);
        if (!NT_SUCCESS(status)) break;

        pMask = (PULONG)inBuf;

        if (*pMask & SERIAL_PURGE_TXABORT) {
            if (pCtx->BulkWritePipe != NULL) {
                WdfUsbTargetPipeAbortSynchronously(
                    pCtx->BulkWritePipe, WDF_NO_HANDLE, NULL);
                WdfUsbTargetPipeResetSynchronously(
                    pCtx->BulkWritePipe, WDF_NO_HANDLE, NULL);
            }
        }
        if (*pMask & SERIAL_PURGE_RXABORT) {
            if (pCtx->BulkReadPipe != NULL) {
                WdfUsbTargetPipeAbortSynchronously(
                    pCtx->BulkReadPipe, WDF_NO_HANDLE, NULL);
                WdfUsbTargetPipeResetSynchronously(
                    pCtx->BulkReadPipe, WDF_NO_HANDLE, NULL);
            }
        }
        /* TXCLEAR / RXCLEAR: no internal software buffers to clear */
        break;
    }

    /* ============================================================
     * Timeouts
     * ============================================================ */
    case IOCTL_SERIAL_SET_TIMEOUTS:
    {
        PSERIAL_TIMEOUTS pTo;
        status = WdfRequestRetrieveInputBuffer(
            Request, sizeof(SERIAL_TIMEOUTS), &inBuf, NULL);
        if (!NT_SUCCESS(status)) break;

        pTo = (PSERIAL_TIMEOUTS)inBuf;
        RtlCopyMemory(&pCtx->Timeouts, pTo, sizeof(SERIAL_TIMEOUTS));
        break;
    }

    case IOCTL_SERIAL_GET_TIMEOUTS:
    {
        PSERIAL_TIMEOUTS pTo;
        status = WdfRequestRetrieveOutputBuffer(
            Request, sizeof(SERIAL_TIMEOUTS), &outBuf, NULL);
        if (!NT_SUCCESS(status)) break;

        pTo = (PSERIAL_TIMEOUTS)outBuf;
        RtlCopyMemory(pTo, &pCtx->Timeouts, sizeof(SERIAL_TIMEOUTS));
        info = sizeof(SERIAL_TIMEOUTS);
        break;
    }

    /* ============================================================
     * Wait mask / event notification
     * ============================================================ */
    case IOCTL_SERIAL_SET_WAIT_MASK:
    {
        PULONG pNewMask;
        WDFREQUEST pendingWait;

        status = WdfRequestRetrieveInputBuffer(
            Request, sizeof(ULONG), &inBuf, NULL);
        if (!NT_SUCCESS(status)) break;

        pNewMask = (PULONG)inBuf;

        WdfWaitLockAcquire(pCtx->SerialLock, NULL);
        pCtx->WaitMask = *pNewMask;

        /* Complete any outstanding WAIT_ON_MASK request with 0 events */
        while (NT_SUCCESS(WdfIoQueueRetrieveNextRequest(
                    pCtx->WaitMaskQueue, &pendingWait))) {
            PULONG pEvents;
            NTSTATUS s2 = WdfRequestRetrieveOutputBuffer(
                pendingWait, sizeof(ULONG), (PVOID *)&pEvents, NULL);
            if (NT_SUCCESS(s2)) {
                *pEvents = 0;
            }
            WdfRequestCompleteWithInformation(
                pendingWait, STATUS_SUCCESS, NT_SUCCESS(s2) ? sizeof(ULONG) : 0);
        }

        WdfWaitLockRelease(pCtx->SerialLock);
        break;
    }

    case IOCTL_SERIAL_GET_WAIT_MASK:
    {
        PULONG pMask;
        status = WdfRequestRetrieveOutputBuffer(
            Request, sizeof(ULONG), &outBuf, NULL);
        if (!NT_SUCCESS(status)) break;

        pMask = (PULONG)outBuf;
        *pMask = pCtx->WaitMask;
        info = sizeof(ULONG);
        break;
    }

    case IOCTL_SERIAL_WAIT_ON_MASK:
    {
        if (pCtx->WaitMask == 0) {
            status = STATUS_INVALID_PARAMETER;
            break;
        }

        /* Park the request in the manual queue; it will be completed
         * when a matching serial event fires or the mask changes. */
        status = WdfRequestForwardToIoQueue(Request, pCtx->WaitMaskQueue);
        if (NT_SUCCESS(status)) {
            return;     /* do NOT complete the request here */
        }
        break;
    }

    /* ============================================================
     * Handshake / flow control
     * ============================================================ */
    case IOCTL_SERIAL_SET_HANDFLOW:
    {
        PSERIAL_HANDFLOW pHf;
        status = WdfRequestRetrieveInputBuffer(
            Request, sizeof(SERIAL_HANDFLOW), &inBuf, NULL);
        if (!NT_SUCCESS(status)) break;

        pHf = (PSERIAL_HANDFLOW)inBuf;
        RtlCopyMemory(&pCtx->HandFlow, pHf, sizeof(SERIAL_HANDFLOW));
        break;
    }

    case IOCTL_SERIAL_GET_HANDFLOW:
    {
        PSERIAL_HANDFLOW pHf;
        status = WdfRequestRetrieveOutputBuffer(
            Request, sizeof(SERIAL_HANDFLOW), &outBuf, NULL);
        if (!NT_SUCCESS(status)) break;

        pHf = (PSERIAL_HANDFLOW)outBuf;
        RtlCopyMemory(pHf, &pCtx->HandFlow, sizeof(SERIAL_HANDFLOW));
        info = sizeof(SERIAL_HANDFLOW);
        break;
    }

    /* ============================================================
     * Special characters (XON, XOFF, etc.)
     * ============================================================ */
    case IOCTL_SERIAL_SET_CHARS:
    {
        PSERIAL_CHARS pCh;
        status = WdfRequestRetrieveInputBuffer(
            Request, sizeof(SERIAL_CHARS), &inBuf, NULL);
        if (!NT_SUCCESS(status)) break;

        pCh = (PSERIAL_CHARS)inBuf;
        RtlCopyMemory(&pCtx->SpecialChars, pCh, sizeof(SERIAL_CHARS));
        break;
    }

    case IOCTL_SERIAL_GET_CHARS:
    {
        PSERIAL_CHARS pCh;
        status = WdfRequestRetrieveOutputBuffer(
            Request, sizeof(SERIAL_CHARS), &outBuf, NULL);
        if (!NT_SUCCESS(status)) break;

        pCh = (PSERIAL_CHARS)outBuf;
        RtlCopyMemory(pCh, &pCtx->SpecialChars, sizeof(SERIAL_CHARS));
        info = sizeof(SERIAL_CHARS);
        break;
    }

    /* ============================================================
     * Modem status
     * ============================================================ */
    case IOCTL_SERIAL_GET_MODEMSTATUS:
    {
        PULONG pMs;
        status = WdfRequestRetrieveOutputBuffer(
            Request, sizeof(ULONG), &outBuf, NULL);
        if (!NT_SUCCESS(status)) break;

        pMs = (PULONG)outBuf;
        *pMs = pCtx->ModemStatus;
        info = sizeof(ULONG);
        break;
    }

    /* ============================================================
     * DTR / RTS read-back
     * ============================================================ */
    case IOCTL_SERIAL_GET_DTRRTS:
    {
        PULONG pDtrRts;
        status = WdfRequestRetrieveOutputBuffer(
            Request, sizeof(ULONG), &outBuf, NULL);
        if (!NT_SUCCESS(status)) break;

        pDtrRts = (PULONG)outBuf;
        *pDtrRts = 0;
        if (pCtx->DtrEnabled) *pDtrRts |= SERIAL_DTR_STATE;
        if (pCtx->RtsEnabled) *pDtrRts |= SERIAL_RTS_STATE;
        info = sizeof(ULONG);
        break;
    }

    /* ============================================================
     * Queue size (informational — no internal buffers to resize)
     * ============================================================ */
    case IOCTL_SERIAL_SET_QUEUE_SIZE:
        /* Accept silently; USB transfers are not queue-based. */
        status = STATUS_SUCCESS;
        break;

    /* ============================================================
     * Unsupported IOCTL
     * ============================================================ */
    default:
        status = STATUS_NOT_SUPPORTED;
        break;
    }

    WdfRequestCompleteWithInformation(Request, status, info);
}

/* ================================================================
 * EvtIoStop — Cancel / acknowledge in-flight requests
 *
 * Called at <= DISPATCH_LEVEL when the queue is being purged
 * (device removal, power-down, etc.).
 * ================================================================ */
VOID
MtkSerialEvtIoStop(
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
