/*
 * serial.c
 *
 * IOCTL_SERIAL_* handlers implementing the Windows serial port interface
 * over USB CDC ACM. Each function handles one IOCTL, validates buffers,
 * updates device state, and completes the request.
 *
 * SPDX-License-Identifier: MIT
 */

#include "mtk_usb2ser.h"

/* ----------------------------------------------------------------
 * IOCTL_SERIAL_SET_BAUD_RATE
 * ---------------------------------------------------------------- */

VOID
SerialSetBaudRate(
    _In_ PDEVICE_CONTEXT DevCtx,
    _In_ WDFREQUEST      Request
    )
{
    NTSTATUS            status;
    PSERIAL_BAUD_RATE   pBaudRate;

    status = WdfRequestRetrieveInputBuffer(
        Request, sizeof(SERIAL_BAUD_RATE), (PVOID *)&pBaudRate, NULL);
    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
        return;
    }

    if (pBaudRate->BaudRate == 0) {
        WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
        return;
    }

    DevCtx->LineCoding.dwDTERate = pBaudRate->BaudRate;
    status = UsbControlSetLineCoding(DevCtx);

    WdfRequestComplete(Request, status);
}

/* ----------------------------------------------------------------
 * IOCTL_SERIAL_GET_BAUD_RATE
 * ---------------------------------------------------------------- */

VOID
SerialGetBaudRate(
    _In_ PDEVICE_CONTEXT DevCtx,
    _In_ WDFREQUEST      Request
    )
{
    NTSTATUS            status;
    PSERIAL_BAUD_RATE   pBaudRate;

    status = WdfRequestRetrieveOutputBuffer(
        Request, sizeof(SERIAL_BAUD_RATE), (PVOID *)&pBaudRate, NULL);
    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
        return;
    }

    pBaudRate->BaudRate = DevCtx->LineCoding.dwDTERate;

    WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS,
                                       sizeof(SERIAL_BAUD_RATE));
}

/* ----------------------------------------------------------------
 * IOCTL_SERIAL_SET_LINE_CONTROL
 * ---------------------------------------------------------------- */

VOID
SerialSetLineControl(
    _In_ PDEVICE_CONTEXT DevCtx,
    _In_ WDFREQUEST      Request
    )
{
    NTSTATUS                status;
    PSERIAL_LINE_CONTROL    pLineCtl;

    status = WdfRequestRetrieveInputBuffer(
        Request, sizeof(SERIAL_LINE_CONTROL), (PVOID *)&pLineCtl, NULL);
    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
        return;
    }

    /* Map stop bits: 0=1stop, 1=1.5stop, 2=2stop */
    switch (pLineCtl->StopBits) {
    case STOP_BIT_1:    DevCtx->LineCoding.bCharFormat = 0; break;
    case STOP_BITS_1_5: DevCtx->LineCoding.bCharFormat = 1; break;
    case STOP_BITS_2:   DevCtx->LineCoding.bCharFormat = 2; break;
    default:
        WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
        return;
    }

    /* Map parity: 0=none, 1=odd, 2=even, 3=mark, 4=space */
    switch (pLineCtl->Parity) {
    case NO_PARITY:     DevCtx->LineCoding.bParityType = 0; break;
    case ODD_PARITY:    DevCtx->LineCoding.bParityType = 1; break;
    case EVEN_PARITY:   DevCtx->LineCoding.bParityType = 2; break;
    case MARK_PARITY:   DevCtx->LineCoding.bParityType = 3; break;
    case SPACE_PARITY:  DevCtx->LineCoding.bParityType = 4; break;
    default:
        WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
        return;
    }

    DevCtx->LineCoding.bDataBits = (UCHAR)pLineCtl->WordLength;

    status = UsbControlSetLineCoding(DevCtx);

    WdfRequestComplete(Request, status);
}

/* ----------------------------------------------------------------
 * IOCTL_SERIAL_GET_LINE_CONTROL
 * ---------------------------------------------------------------- */

VOID
SerialGetLineControl(
    _In_ PDEVICE_CONTEXT DevCtx,
    _In_ WDFREQUEST      Request
    )
{
    NTSTATUS                status;
    PSERIAL_LINE_CONTROL    pLineCtl;

    status = WdfRequestRetrieveOutputBuffer(
        Request, sizeof(SERIAL_LINE_CONTROL), (PVOID *)&pLineCtl, NULL);
    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
        return;
    }

    switch (DevCtx->LineCoding.bCharFormat) {
    case 0:  pLineCtl->StopBits = STOP_BIT_1;    break;
    case 1:  pLineCtl->StopBits = STOP_BITS_1_5;  break;
    case 2:  pLineCtl->StopBits = STOP_BITS_2;    break;
    default: pLineCtl->StopBits = STOP_BIT_1;    break;
    }

    switch (DevCtx->LineCoding.bParityType) {
    case 0:  pLineCtl->Parity = NO_PARITY;    break;
    case 1:  pLineCtl->Parity = ODD_PARITY;   break;
    case 2:  pLineCtl->Parity = EVEN_PARITY;  break;
    case 3:  pLineCtl->Parity = MARK_PARITY;  break;
    case 4:  pLineCtl->Parity = SPACE_PARITY;  break;
    default: pLineCtl->Parity = NO_PARITY;    break;
    }

    pLineCtl->WordLength = DevCtx->LineCoding.bDataBits;

    WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS,
                                       sizeof(SERIAL_LINE_CONTROL));
}

/* ----------------------------------------------------------------
 * IOCTL_SERIAL_SET_HANDFLOW
 * ---------------------------------------------------------------- */

VOID
SerialSetHandflow(
    _In_ PDEVICE_CONTEXT DevCtx,
    _In_ WDFREQUEST      Request
    )
{
    NTSTATUS            status;
    PSERIAL_HANDFLOW    pHandflow;

    status = WdfRequestRetrieveInputBuffer(
        Request, sizeof(SERIAL_HANDFLOW), (PVOID *)&pHandflow, NULL);
    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
        return;
    }

    DevCtx->HandFlow = *pHandflow;

    WdfRequestComplete(Request, STATUS_SUCCESS);
}

/* ----------------------------------------------------------------
 * IOCTL_SERIAL_GET_HANDFLOW
 * ---------------------------------------------------------------- */

VOID
SerialGetHandflow(
    _In_ PDEVICE_CONTEXT DevCtx,
    _In_ WDFREQUEST      Request
    )
{
    NTSTATUS            status;
    PSERIAL_HANDFLOW    pHandflow;

    status = WdfRequestRetrieveOutputBuffer(
        Request, sizeof(SERIAL_HANDFLOW), (PVOID *)&pHandflow, NULL);
    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
        return;
    }

    *pHandflow = DevCtx->HandFlow;

    WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS,
                                       sizeof(SERIAL_HANDFLOW));
}

/* ----------------------------------------------------------------
 * IOCTL_SERIAL_SET_DTR / CLR_DTR
 * ---------------------------------------------------------------- */

VOID
SerialSetDtr(
    _In_ PDEVICE_CONTEXT DevCtx,
    _In_ WDFREQUEST      Request
    )
{
    NTSTATUS status = UsbControlSetDtr(DevCtx);
    WdfRequestComplete(Request, status);
}

VOID
SerialClrDtr(
    _In_ PDEVICE_CONTEXT DevCtx,
    _In_ WDFREQUEST      Request
    )
{
    NTSTATUS status = UsbControlClrDtr(DevCtx);
    WdfRequestComplete(Request, status);
}

/* ----------------------------------------------------------------
 * IOCTL_SERIAL_SET_RTS / CLR_RTS
 * ---------------------------------------------------------------- */

VOID
SerialSetRts(
    _In_ PDEVICE_CONTEXT DevCtx,
    _In_ WDFREQUEST      Request
    )
{
    NTSTATUS status = UsbControlSetRts(DevCtx);
    WdfRequestComplete(Request, status);
}

VOID
SerialClrRts(
    _In_ PDEVICE_CONTEXT DevCtx,
    _In_ WDFREQUEST      Request
    )
{
    NTSTATUS status = UsbControlClrRts(DevCtx);
    WdfRequestComplete(Request, status);
}

/* ----------------------------------------------------------------
 * IOCTL_SERIAL_GET_DTRRTS
 * ---------------------------------------------------------------- */

VOID
SerialGetDtrRts(
    _In_ PDEVICE_CONTEXT DevCtx,
    _In_ WDFREQUEST      Request
    )
{
    NTSTATUS    status;
    PULONG      pDtrRts;

    status = WdfRequestRetrieveOutputBuffer(
        Request, sizeof(ULONG), (PVOID *)&pDtrRts, NULL);
    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
        return;
    }

    *pDtrRts = 0;
    if (DevCtx->DtrState) *pDtrRts |= SERIAL_DTR_STATE;
    if (DevCtx->RtsState) *pDtrRts |= SERIAL_RTS_STATE;

    WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, sizeof(ULONG));
}

/* ----------------------------------------------------------------
 * IOCTL_SERIAL_GET_MODEMSTATUS
 * ---------------------------------------------------------------- */

VOID
SerialGetModemStatus(
    _In_ PDEVICE_CONTEXT DevCtx,
    _In_ WDFREQUEST      Request
    )
{
    NTSTATUS    status;
    PULONG      pModemStatus;

    status = WdfRequestRetrieveOutputBuffer(
        Request, sizeof(ULONG), (PVOID *)&pModemStatus, NULL);
    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
        return;
    }

    *pModemStatus = DevCtx->ModemStatus;

    WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, sizeof(ULONG));
}

/* ----------------------------------------------------------------
 * IOCTL_SERIAL_SET_BREAK_ON / SET_BREAK_OFF
 * ---------------------------------------------------------------- */

VOID
SerialSetBreakOn(
    _In_ PDEVICE_CONTEXT DevCtx,
    _In_ WDFREQUEST      Request
    )
{
    NTSTATUS status = UsbControlSendBreak(DevCtx, 0xFFFF);
    WdfRequestComplete(Request, status);
}

VOID
SerialSetBreakOff(
    _In_ PDEVICE_CONTEXT DevCtx,
    _In_ WDFREQUEST      Request
    )
{
    NTSTATUS status = UsbControlSendBreak(DevCtx, 0);
    WdfRequestComplete(Request, status);
}

/* ----------------------------------------------------------------
 * IOCTL_SERIAL_SET_TIMEOUTS / GET_TIMEOUTS
 * ---------------------------------------------------------------- */

VOID
SerialSetTimeouts(
    _In_ PDEVICE_CONTEXT DevCtx,
    _In_ WDFREQUEST      Request
    )
{
    NTSTATUS            status;
    PSERIAL_TIMEOUTS    pTimeouts;

    status = WdfRequestRetrieveInputBuffer(
        Request, sizeof(SERIAL_TIMEOUTS), (PVOID *)&pTimeouts, NULL);
    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
        return;
    }

    DevCtx->Timeouts = *pTimeouts;

    WdfRequestComplete(Request, STATUS_SUCCESS);
}

VOID
SerialGetTimeouts(
    _In_ PDEVICE_CONTEXT DevCtx,
    _In_ WDFREQUEST      Request
    )
{
    NTSTATUS            status;
    PSERIAL_TIMEOUTS    pTimeouts;

    status = WdfRequestRetrieveOutputBuffer(
        Request, sizeof(SERIAL_TIMEOUTS), (PVOID *)&pTimeouts, NULL);
    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
        return;
    }

    *pTimeouts = DevCtx->Timeouts;

    WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS,
                                       sizeof(SERIAL_TIMEOUTS));
}

/* ----------------------------------------------------------------
 * IOCTL_SERIAL_SET_CHARS / GET_CHARS
 * ---------------------------------------------------------------- */

VOID
SerialSetChars(
    _In_ PDEVICE_CONTEXT DevCtx,
    _In_ WDFREQUEST      Request
    )
{
    NTSTATUS        status;
    PSERIAL_CHARS   pChars;

    status = WdfRequestRetrieveInputBuffer(
        Request, sizeof(SERIAL_CHARS), (PVOID *)&pChars, NULL);
    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
        return;
    }

    DevCtx->SpecialChars = *pChars;

    WdfRequestComplete(Request, STATUS_SUCCESS);
}

VOID
SerialGetChars(
    _In_ PDEVICE_CONTEXT DevCtx,
    _In_ WDFREQUEST      Request
    )
{
    NTSTATUS        status;
    PSERIAL_CHARS   pChars;

    status = WdfRequestRetrieveOutputBuffer(
        Request, sizeof(SERIAL_CHARS), (PVOID *)&pChars, NULL);
    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
        return;
    }

    *pChars = DevCtx->SpecialChars;

    WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS,
                                       sizeof(SERIAL_CHARS));
}

/* ----------------------------------------------------------------
 * IOCTL_SERIAL_SET_QUEUE_SIZE
 * ---------------------------------------------------------------- */

VOID
SerialSetQueueSize(
    _In_ PDEVICE_CONTEXT DevCtx,
    _In_ WDFREQUEST      Request
    )
{
    NTSTATUS                status;
    PSERIAL_QUEUE_SIZE      pQueueSize;

    status = WdfRequestRetrieveInputBuffer(
        Request, sizeof(SERIAL_QUEUE_SIZE), (PVOID *)&pQueueSize, NULL);
    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
        return;
    }

    DevCtx->InQueueSize  = pQueueSize->InSize;
    DevCtx->OutQueueSize = pQueueSize->OutSize;

    WdfRequestComplete(Request, STATUS_SUCCESS);
}

/* ----------------------------------------------------------------
 * IOCTL_SERIAL_SET_WAIT_MASK / GET_WAIT_MASK
 * ---------------------------------------------------------------- */

VOID
SerialSetWaitMask(
    _In_ PDEVICE_CONTEXT DevCtx,
    _In_ WDFREQUEST      Request
    )
{
    NTSTATUS    status;
    PULONG      pMask;
    ULONG       oldMask;

    status = WdfRequestRetrieveInputBuffer(
        Request, sizeof(ULONG), (PVOID *)&pMask, NULL);
    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
        return;
    }

    WdfSpinLockAcquire(DevCtx->EventLock);
    oldMask = DevCtx->WaitMask;
    DevCtx->WaitMask = *pMask;
    DevCtx->EventHistory = 0;
    WdfSpinLockRelease(DevCtx->EventLock);

    /* If the mask changed, complete any pending WAIT_ON_MASK with 0 */
    if (oldMask != *pMask) {
        WDFREQUEST  waitRequest;
        NTSTATUS    waitStatus;

        waitStatus = WdfIoQueueRetrieveNextRequest(
            DevCtx->PendingWaitMaskQueue, &waitRequest);
        if (NT_SUCCESS(waitStatus)) {
            PULONG pEvents;
            waitStatus = WdfRequestRetrieveOutputBuffer(
                waitRequest, sizeof(ULONG), (PVOID *)&pEvents, NULL);
            if (NT_SUCCESS(waitStatus)) {
                *pEvents = 0;
                WdfRequestCompleteWithInformation(waitRequest,
                                                   STATUS_SUCCESS,
                                                   sizeof(ULONG));
            } else {
                WdfRequestComplete(waitRequest, waitStatus);
            }
        }
    }

    WdfRequestComplete(Request, STATUS_SUCCESS);
}

VOID
SerialGetWaitMask(
    _In_ PDEVICE_CONTEXT DevCtx,
    _In_ WDFREQUEST      Request
    )
{
    NTSTATUS    status;
    PULONG      pMask;

    status = WdfRequestRetrieveOutputBuffer(
        Request, sizeof(ULONG), (PVOID *)&pMask, NULL);
    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
        return;
    }

    *pMask = DevCtx->WaitMask;

    WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, sizeof(ULONG));
}

/* ----------------------------------------------------------------
 * IOCTL_SERIAL_WAIT_ON_MASK
 * ---------------------------------------------------------------- */

VOID
SerialWaitOnMask(
    _In_ PDEVICE_CONTEXT DevCtx,
    _In_ WDFREQUEST      Request
    )
{
    NTSTATUS    status;
    PULONG      pEvents;
    ULONG       pendingEvents;

    status = WdfRequestRetrieveOutputBuffer(
        Request, sizeof(ULONG), (PVOID *)&pEvents, NULL);
    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
        return;
    }

    WdfSpinLockAcquire(DevCtx->EventLock);
    pendingEvents = DevCtx->EventHistory & DevCtx->WaitMask;
    DevCtx->EventHistory &= ~pendingEvents;
    WdfSpinLockRelease(DevCtx->EventLock);

    if (pendingEvents != 0) {
        *pEvents = pendingEvents;
        WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS,
                                           sizeof(ULONG));
        return;
    }

    /* No events yet, pend the request */
    status = WdfRequestForwardToIoQueue(Request,
                                         DevCtx->PendingWaitMaskQueue);
    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
    }
}

/* ----------------------------------------------------------------
 * SerialCompleteWaitOnMask - called from interrupt/completion context
 * ---------------------------------------------------------------- */

VOID
SerialCompleteWaitOnMask(
    _In_ PDEVICE_CONTEXT DevCtx,
    _In_ ULONG           Events
    )
{
    WDFREQUEST  request;
    NTSTATUS    status;
    PULONG      pEvents;
    ULONG       maskedEvents;

    WdfSpinLockAcquire(DevCtx->EventLock);
    DevCtx->EventHistory |= Events;
    maskedEvents = DevCtx->EventHistory & DevCtx->WaitMask;
    if (maskedEvents != 0) {
        DevCtx->EventHistory &= ~maskedEvents;
    }
    WdfSpinLockRelease(DevCtx->EventLock);

    if (maskedEvents == 0) {
        return;
    }

    status = WdfIoQueueRetrieveNextRequest(
        DevCtx->PendingWaitMaskQueue, &request);
    if (!NT_SUCCESS(status)) {
        /* Re-record the events if nobody was waiting */
        WdfSpinLockAcquire(DevCtx->EventLock);
        DevCtx->EventHistory |= maskedEvents;
        WdfSpinLockRelease(DevCtx->EventLock);
        return;
    }

    status = WdfRequestRetrieveOutputBuffer(
        request, sizeof(ULONG), (PVOID *)&pEvents, NULL);
    if (NT_SUCCESS(status)) {
        *pEvents = maskedEvents;
        WdfRequestCompleteWithInformation(request, STATUS_SUCCESS,
                                           sizeof(ULONG));
    } else {
        WdfRequestComplete(request, status);
    }
}

/* ----------------------------------------------------------------
 * IOCTL_SERIAL_PURGE
 * ---------------------------------------------------------------- */

VOID
SerialPurge(
    _In_ PDEVICE_CONTEXT DevCtx,
    _In_ WDFREQUEST      Request
    )
{
    NTSTATUS    status;
    PULONG      pPurge;

    status = WdfRequestRetrieveInputBuffer(
        Request, sizeof(ULONG), (PVOID *)&pPurge, NULL);
    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
        return;
    }

    if (*pPurge & SERIAL_PURGE_RXCLEAR) {
        RingBufferPurge(&DevCtx->ReadBuffer);
    }

    if (*pPurge & SERIAL_PURGE_RXABORT) {
        WdfIoQueuePurgeSynchronously(DevCtx->PendingReadQueue);
        WdfIoQueueStart(DevCtx->PendingReadQueue);
    }

    if (*pPurge & SERIAL_PURGE_TXABORT) {
        /* TX abort: best effort - USB writes in flight complete normally */
    }

    if (*pPurge & SERIAL_PURGE_TXCLEAR) {
        /* TX clear: no software TX buffer to clear in this implementation */
    }

    WdfRequestComplete(Request, STATUS_SUCCESS);
}

/* ----------------------------------------------------------------
 * IOCTL_SERIAL_GET_COMMSTATUS
 * ---------------------------------------------------------------- */

VOID
SerialGetCommStatus(
    _In_ PDEVICE_CONTEXT DevCtx,
    _In_ WDFREQUEST      Request
    )
{
    NTSTATUS            status;
    PSERIAL_STATUS      pStatus;

    status = WdfRequestRetrieveOutputBuffer(
        Request, sizeof(SERIAL_STATUS), (PVOID *)&pStatus, NULL);
    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
        return;
    }

    RtlZeroMemory(pStatus, sizeof(SERIAL_STATUS));

    pStatus->AmountInInQueue  = RingBufferGetDataLength(&DevCtx->ReadBuffer);
    pStatus->AmountInOutQueue = 0;
    pStatus->EofReceived      = FALSE;
    pStatus->WaitForImmediate = FALSE;

    WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS,
                                       sizeof(SERIAL_STATUS));
}

/* ----------------------------------------------------------------
 * IOCTL_SERIAL_GET_PROPERTIES
 * ---------------------------------------------------------------- */

VOID
SerialGetProperties(
    _In_ PDEVICE_CONTEXT DevCtx,
    _In_ WDFREQUEST      Request
    )
{
    NTSTATUS                status;
    PSERIAL_COMMPROP        pProps;

    UNREFERENCED_PARAMETER(DevCtx);

    status = WdfRequestRetrieveOutputBuffer(
        Request, sizeof(SERIAL_COMMPROP), (PVOID *)&pProps, NULL);
    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
        return;
    }

    RtlZeroMemory(pProps, sizeof(SERIAL_COMMPROP));

    pProps->PacketLength       = sizeof(SERIAL_COMMPROP);
    pProps->PacketVersion      = 2;
    pProps->ServiceMask        = SERIAL_SP_SERIALCOMM;
    pProps->MaxTxQueue         = 0;
    pProps->MaxRxQueue         = 0;
    pProps->MaxBaud            = SERIAL_BAUD_USER;
    pProps->ProvSubType        = SERIAL_SP_RS232;
    pProps->ProvCapabilities   = SERIAL_PCF_DTRDSR |
                                  SERIAL_PCF_RTSCTS |
                                  SERIAL_PCF_CD |
                                  SERIAL_PCF_TOTALTIMEOUTS |
                                  SERIAL_PCF_INTTIMEOUTS;
    pProps->SettableParams     = SERIAL_SP_PARITY |
                                  SERIAL_SP_BAUD |
                                  SERIAL_SP_DATABITS |
                                  SERIAL_SP_STOPBITS |
                                  SERIAL_SP_HANDSHAKING |
                                  SERIAL_SP_PARITY_CHECK |
                                  SERIAL_SP_CARRIER_DETECT;
    pProps->SettableBaud       = SERIAL_BAUD_075 |
                                  SERIAL_BAUD_110 |
                                  SERIAL_BAUD_150 |
                                  SERIAL_BAUD_300 |
                                  SERIAL_BAUD_600 |
                                  SERIAL_BAUD_1200 |
                                  SERIAL_BAUD_1800 |
                                  SERIAL_BAUD_2400 |
                                  SERIAL_BAUD_4800 |
                                  SERIAL_BAUD_7200 |
                                  SERIAL_BAUD_9600 |
                                  SERIAL_BAUD_14400 |
                                  SERIAL_BAUD_19200 |
                                  SERIAL_BAUD_38400 |
                                  SERIAL_BAUD_56K |
                                  SERIAL_BAUD_57600 |
                                  SERIAL_BAUD_115200 |
                                  SERIAL_BAUD_128K |
                                  SERIAL_BAUD_USER;
    pProps->SettableData       = SERIAL_DATABITS_5 |
                                  SERIAL_DATABITS_6 |
                                  SERIAL_DATABITS_7 |
                                  SERIAL_DATABITS_8;
    pProps->SettableStopParity = SERIAL_STOPBITS_10 |
                                  SERIAL_STOPBITS_15 |
                                  SERIAL_STOPBITS_20 |
                                  SERIAL_PARITY_NONE |
                                  SERIAL_PARITY_ODD |
                                  SERIAL_PARITY_EVEN |
                                  SERIAL_PARITY_MARK |
                                  SERIAL_PARITY_SPACE;
    pProps->CurrentTxQueue     = DevCtx->OutQueueSize;
    pProps->CurrentRxQueue     = DevCtx->InQueueSize;

    WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS,
                                       sizeof(SERIAL_COMMPROP));
}

/* ----------------------------------------------------------------
 * IOCTL_SERIAL_GET_STATS / CLEAR_STATS
 * ---------------------------------------------------------------- */

VOID
SerialGetStats(
    _In_ PDEVICE_CONTEXT DevCtx,
    _In_ WDFREQUEST      Request
    )
{
    NTSTATUS            status;
    PSERIALPERF_STATS   pStats;

    status = WdfRequestRetrieveOutputBuffer(
        Request, sizeof(SERIALPERF_STATS), (PVOID *)&pStats, NULL);
    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
        return;
    }

    *pStats = DevCtx->PerfStats;

    WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS,
                                       sizeof(SERIALPERF_STATS));
}

VOID
SerialClearStats(
    _In_ PDEVICE_CONTEXT DevCtx,
    _In_ WDFREQUEST      Request
    )
{
    RtlZeroMemory(&DevCtx->PerfStats, sizeof(SERIALPERF_STATS));
    WdfRequestComplete(Request, STATUS_SUCCESS);
}

/* ----------------------------------------------------------------
 * IOCTL_SERIAL_CONFIG_SIZE
 * ---------------------------------------------------------------- */

VOID
SerialConfigSize(
    _In_ PDEVICE_CONTEXT DevCtx,
    _In_ WDFREQUEST      Request
    )
{
    NTSTATUS    status;
    PULONG      pConfigSize;

    UNREFERENCED_PARAMETER(DevCtx);

    status = WdfRequestRetrieveOutputBuffer(
        Request, sizeof(ULONG), (PVOID *)&pConfigSize, NULL);
    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
        return;
    }

    *pConfigSize = 0;

    WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, sizeof(ULONG));
}

/* ----------------------------------------------------------------
 * IOCTL_SERIAL_LSRMST_INSERT
 * ---------------------------------------------------------------- */

VOID
SerialLsrMstInsert(
    _In_ PDEVICE_CONTEXT DevCtx,
    _In_ WDFREQUEST      Request
    )
{
    NTSTATUS    status;
    PUCHAR      pInsert;

    status = WdfRequestRetrieveInputBuffer(
        Request, sizeof(UCHAR), (PVOID *)&pInsert, NULL);
    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
        return;
    }

    DevCtx->LsrMstInsert = *pInsert;
    DevCtx->LsrMstInsertEnabled = (*pInsert != 0);

    WdfRequestComplete(Request, STATUS_SUCCESS);
}
