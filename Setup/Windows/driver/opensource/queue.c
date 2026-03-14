/*
 * queue.c
 *
 * I/O queue setup and dispatch for Read, Write, and DeviceControl.
 * Creates DefaultQueue (parallel), PendingReadQueue (manual),
 * and PendingWaitMaskQueue (manual).
 *
 * SPDX-License-Identifier: MIT
 */

#include "mtk_usb2ser.h"

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, QueueInitialize)
#endif

NTSTATUS
QueueInitialize(
    _In_ WDFDEVICE Device
    )
{
    NTSTATUS            status;
    PDEVICE_CONTEXT     devCtx = GetDeviceContext(Device);
    WDF_IO_QUEUE_CONFIG queueConfig;

    PAGED_CODE();

    /* Default parallel queue for Read, Write, DeviceControl */
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, WdfIoQueueDispatchParallel);
    queueConfig.EvtIoRead          = EvtIoRead;
    queueConfig.EvtIoWrite         = EvtIoWrite;
    queueConfig.EvtIoDeviceControl = EvtIoDeviceControl;
    queueConfig.PowerManaged       = WdfTrue;

    status = WdfIoQueueCreate(Device, &queueConfig,
                              WDF_NO_OBJECT_ATTRIBUTES,
                              &devCtx->DefaultQueue);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    /* Manual queue for pending read requests */
    WDF_IO_QUEUE_CONFIG_INIT(&queueConfig, WdfIoQueueDispatchManual);
    queueConfig.PowerManaged = WdfTrue;

    status = WdfIoQueueCreate(Device, &queueConfig,
                              WDF_NO_OBJECT_ATTRIBUTES,
                              &devCtx->PendingReadQueue);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    /* Manual queue for pending WAIT_ON_MASK requests */
    WDF_IO_QUEUE_CONFIG_INIT(&queueConfig, WdfIoQueueDispatchManual);
    queueConfig.PowerManaged = WdfTrue;

    status = WdfIoQueueCreate(Device, &queueConfig,
                              WDF_NO_OBJECT_ATTRIBUTES,
                              &devCtx->PendingWaitMaskQueue);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    return STATUS_SUCCESS;
}

VOID
EvtIoRead(
    _In_ WDFQUEUE   Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t     Length
    )
{
    WDFDEVICE       device = WdfIoQueueGetDevice(Queue);
    PDEVICE_CONTEXT devCtx = GetDeviceContext(device);
    NTSTATUS        status;
    PVOID           outputBuffer;
    ULONG           dataAvailable;
    ULONG           bytesRead;

    UNREFERENCED_PARAMETER(Length);

    status = WdfRequestRetrieveOutputBuffer(Request, 1, &outputBuffer, NULL);
    if (!NT_SUCCESS(status)) {
        WdfRequestCompleteWithInformation(Request, status, 0);
        return;
    }

    dataAvailable = RingBufferGetDataLength(&devCtx->ReadBuffer);
    if (dataAvailable > 0) {
        ULONG requestLength = (ULONG)Length;
        if (requestLength > dataAvailable) {
            requestLength = dataAvailable;
        }
        bytesRead = RingBufferRead(&devCtx->ReadBuffer,
                                   (PUCHAR)outputBuffer,
                                   requestLength);
        devCtx->PerfStats.ReceivedCount += bytesRead;
        WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, bytesRead);
        return;
    }

    /* Non-blocking if ReadIntervalTimeout == MAXULONG and constants are 0 */
    if (devCtx->Timeouts.ReadIntervalTimeout == MAXULONG &&
        devCtx->Timeouts.ReadTotalTimeoutMultiplier == 0 &&
        devCtx->Timeouts.ReadTotalTimeoutConstant == 0) {
        WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, 0);
        return;
    }

    /* Forward to pending read queue to be completed when data arrives */
    status = WdfRequestForwardToIoQueue(Request, devCtx->PendingReadQueue);
    if (!NT_SUCCESS(status)) {
        WdfRequestCompleteWithInformation(Request, status, 0);
    }
}

VOID
EvtIoWrite(
    _In_ WDFQUEUE   Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t     Length
    )
{
    WDFDEVICE               device = WdfIoQueueGetDevice(Queue);
    PDEVICE_CONTEXT         devCtx = GetDeviceContext(device);
    NTSTATUS                status;
    PVOID                   inputBuffer;
    WDFMEMORY               reqMemory;
    WDF_MEMORY_DESCRIPTOR   memDesc;
    BOOLEAN                 sendZlp = FALSE;

    UNREFERENCED_PARAMETER(Length);

    status = WdfRequestRetrieveInputBuffer(Request, 1, &inputBuffer, NULL);
    if (!NT_SUCCESS(status)) {
        WdfRequestCompleteWithInformation(Request, status, 0);
        return;
    }

    status = WdfRequestRetrieveInputMemory(Request, &reqMemory);
    if (!NT_SUCCESS(status)) {
        WdfRequestCompleteWithInformation(Request, status, 0);
        return;
    }

    /* Check if we need a ZLP after this transfer */
    if (devCtx->BulkOutMaxPacket > 0 &&
        Length > 0 &&
        (Length % devCtx->BulkOutMaxPacket) == 0) {
        sendZlp = TRUE;
    }

    InterlockedIncrement(&devCtx->OutstandingWrites);

    WDF_MEMORY_DESCRIPTOR_INIT_HANDLE(&memDesc, reqMemory, NULL);

    status = WdfUsbTargetPipeFormatRequestForWrite(
        devCtx->BulkOutPipe,
        Request,
        reqMemory,
        NULL
        );
    if (!NT_SUCCESS(status)) {
        InterlockedDecrement(&devCtx->OutstandingWrites);
        WdfRequestCompleteWithInformation(Request, status, 0);
        return;
    }

    WdfRequestSetCompletionRoutine(Request, EvtWriteRequestComplete, devCtx);

    if (sendZlp) {
        WdfSpinLockAcquire(devCtx->WriteLock);
        devCtx->PendingZlpCompleteRequest = Request;
        devCtx->ZlpBytesWritten = Length;
        WdfSpinLockRelease(devCtx->WriteLock);
    }

    if (!WdfRequestSend(Request,
                        WdfUsbTargetPipeGetIoTarget(devCtx->BulkOutPipe),
                        WDF_NO_SEND_OPTIONS)) {
        status = WdfRequestGetStatus(Request);
        InterlockedDecrement(&devCtx->OutstandingWrites);

        if (sendZlp) {
            WdfSpinLockAcquire(devCtx->WriteLock);
            devCtx->PendingZlpCompleteRequest = NULL;
            WdfSpinLockRelease(devCtx->WriteLock);
        }

        WdfRequestCompleteWithInformation(Request, status, 0);
    }
}

VOID
EvtIoDeviceControl(
    _In_ WDFQUEUE   Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t     OutputBufferLength,
    _In_ size_t     InputBufferLength,
    _In_ ULONG      IoControlCode
    )
{
    WDFDEVICE       device = WdfIoQueueGetDevice(Queue);
    PDEVICE_CONTEXT devCtx = GetDeviceContext(device);

    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);

    switch (IoControlCode) {

    case IOCTL_SERIAL_SET_BAUD_RATE:
        SerialSetBaudRate(devCtx, Request);
        break;

    case IOCTL_SERIAL_GET_BAUD_RATE:
        SerialGetBaudRate(devCtx, Request);
        break;

    case IOCTL_SERIAL_SET_LINE_CONTROL:
        SerialSetLineControl(devCtx, Request);
        break;

    case IOCTL_SERIAL_GET_LINE_CONTROL:
        SerialGetLineControl(devCtx, Request);
        break;

    case IOCTL_SERIAL_SET_HANDFLOW:
        SerialSetHandflow(devCtx, Request);
        break;

    case IOCTL_SERIAL_GET_HANDFLOW:
        SerialGetHandflow(devCtx, Request);
        break;

    case IOCTL_SERIAL_SET_DTR:
        SerialSetDtr(devCtx, Request);
        break;

    case IOCTL_SERIAL_CLR_DTR:
        SerialClrDtr(devCtx, Request);
        break;

    case IOCTL_SERIAL_SET_RTS:
        SerialSetRts(devCtx, Request);
        break;

    case IOCTL_SERIAL_CLR_RTS:
        SerialClrRts(devCtx, Request);
        break;

    case IOCTL_SERIAL_GET_DTRRTS:
        SerialGetDtrRts(devCtx, Request);
        break;

    case IOCTL_SERIAL_GET_MODEMSTATUS:
        SerialGetModemStatus(devCtx, Request);
        break;

    case IOCTL_SERIAL_SET_BREAK_ON:
        SerialSetBreakOn(devCtx, Request);
        break;

    case IOCTL_SERIAL_SET_BREAK_OFF:
        SerialSetBreakOff(devCtx, Request);
        break;

    case IOCTL_SERIAL_SET_TIMEOUTS:
        SerialSetTimeouts(devCtx, Request);
        break;

    case IOCTL_SERIAL_GET_TIMEOUTS:
        SerialGetTimeouts(devCtx, Request);
        break;

    case IOCTL_SERIAL_SET_CHARS:
        SerialSetChars(devCtx, Request);
        break;

    case IOCTL_SERIAL_GET_CHARS:
        SerialGetChars(devCtx, Request);
        break;

    case IOCTL_SERIAL_SET_QUEUE_SIZE:
        SerialSetQueueSize(devCtx, Request);
        break;

    case IOCTL_SERIAL_SET_WAIT_MASK:
        SerialSetWaitMask(devCtx, Request);
        break;

    case IOCTL_SERIAL_GET_WAIT_MASK:
        SerialGetWaitMask(devCtx, Request);
        break;

    case IOCTL_SERIAL_WAIT_ON_MASK:
        SerialWaitOnMask(devCtx, Request);
        break;

    case IOCTL_SERIAL_PURGE:
        SerialPurge(devCtx, Request);
        break;

    case IOCTL_SERIAL_GET_COMMSTATUS:
        SerialGetCommStatus(devCtx, Request);
        break;

    case IOCTL_SERIAL_GET_PROPERTIES:
        SerialGetProperties(devCtx, Request);
        break;

    case IOCTL_SERIAL_GET_STATS:
        SerialGetStats(devCtx, Request);
        break;

    case IOCTL_SERIAL_CLEAR_STATS:
        SerialClearStats(devCtx, Request);
        break;

    case IOCTL_SERIAL_CONFIG_SIZE:
        SerialConfigSize(devCtx, Request);
        break;

    case IOCTL_SERIAL_LSRMST_INSERT:
        SerialLsrMstInsert(devCtx, Request);
        break;

    case IOCTL_SERIAL_XOFF_COUNTER:
    case IOCTL_SERIAL_IMMEDIATE_CHAR:
    case IOCTL_SERIAL_RESET_DEVICE:
        WdfRequestComplete(Request, STATUS_SUCCESS);
        break;

    default:
        WdfRequestComplete(Request, STATUS_INVALID_DEVICE_REQUEST);
        break;
    }
}
