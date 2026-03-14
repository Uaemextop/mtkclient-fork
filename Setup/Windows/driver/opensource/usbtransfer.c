/*
 * usbtransfer.c
 *
 * Ring buffer implementation, continuous reader completion callbacks
 * for bulk IN and interrupt endpoints, write completion routine,
 * and ZLP work item.
 *
 * SPDX-License-Identifier: MIT
 */

#include "mtk_usb2ser.h"

/* ----------------------------------------------------------------
 * Ring Buffer
 * ---------------------------------------------------------------- */

NTSTATUS
RingBufferInit(
    _In_ PRING_BUFFER Rb,
    _In_ ULONG        Size
    )
{
    if (Rb->Buffer != NULL) {
        return STATUS_SUCCESS;
    }

    Rb->Buffer = (PUCHAR)ExAllocatePool2(POOL_FLAG_NON_PAGED,
                                          Size, MTK_POOL_TAG);
    if (Rb->Buffer == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Rb->Size        = Size;
    Rb->ReadOffset  = 0;
    Rb->WriteOffset = 0;
    Rb->DataLength  = 0;

    return STATUS_SUCCESS;
}

VOID
RingBufferFree(
    _In_ PRING_BUFFER Rb
    )
{
    if (Rb->Buffer != NULL) {
        ExFreePoolWithTag(Rb->Buffer, MTK_POOL_TAG);
        Rb->Buffer = NULL;
    }
    Rb->Size        = 0;
    Rb->ReadOffset  = 0;
    Rb->WriteOffset = 0;
    Rb->DataLength  = 0;
}

ULONG
RingBufferWrite(
    _In_ PRING_BUFFER           Rb,
    _In_reads_(Length) PUCHAR    Data,
    _In_ ULONG                  Length
    )
{
    ULONG available;
    ULONG written = 0;
    ULONG chunk;

    WdfSpinLockAcquire(Rb->Lock);

    available = Rb->Size - Rb->DataLength;
    if (Length > available) {
        Length = available;
    }

    while (written < Length) {
        chunk = Rb->Size - Rb->WriteOffset;
        if (chunk > (Length - written)) {
            chunk = Length - written;
        }
        RtlCopyMemory(Rb->Buffer + Rb->WriteOffset, Data + written, chunk);
        Rb->WriteOffset = (Rb->WriteOffset + chunk) % Rb->Size;
        written += chunk;
    }

    Rb->DataLength += written;

    WdfSpinLockRelease(Rb->Lock);

    return written;
}

ULONG
RingBufferRead(
    _In_ PRING_BUFFER               Rb,
    _Out_writes_(Length) PUCHAR      Data,
    _In_ ULONG                      Length
    )
{
    ULONG bytesRead = 0;
    ULONG chunk;

    WdfSpinLockAcquire(Rb->Lock);

    if (Length > Rb->DataLength) {
        Length = Rb->DataLength;
    }

    while (bytesRead < Length) {
        chunk = Rb->Size - Rb->ReadOffset;
        if (chunk > (Length - bytesRead)) {
            chunk = Length - bytesRead;
        }
        RtlCopyMemory(Data + bytesRead, Rb->Buffer + Rb->ReadOffset, chunk);
        Rb->ReadOffset = (Rb->ReadOffset + chunk) % Rb->Size;
        bytesRead += chunk;
    }

    Rb->DataLength -= bytesRead;

    WdfSpinLockRelease(Rb->Lock);

    return bytesRead;
}

VOID
RingBufferPurge(
    _In_ PRING_BUFFER Rb
    )
{
    WdfSpinLockAcquire(Rb->Lock);
    Rb->ReadOffset  = 0;
    Rb->WriteOffset = 0;
    Rb->DataLength  = 0;
    WdfSpinLockRelease(Rb->Lock);
}

ULONG
RingBufferGetDataLength(
    _In_ PRING_BUFFER Rb
    )
{
    ULONG length;

    WdfSpinLockAcquire(Rb->Lock);
    length = Rb->DataLength;
    WdfSpinLockRelease(Rb->Lock);

    return length;
}

/* ----------------------------------------------------------------
 * Continuous Reader Callbacks - Bulk IN
 * ---------------------------------------------------------------- */

VOID
EvtUsbBulkInReadComplete(
    _In_ WDFUSBPIPE Pipe,
    _In_ WDFMEMORY  Buffer,
    _In_ size_t     NumBytesTransferred,
    _In_ WDFCONTEXT Context
    )
{
    PDEVICE_CONTEXT devCtx = (PDEVICE_CONTEXT)Context;
    PUCHAR          data;

    UNREFERENCED_PARAMETER(Pipe);

    if (NumBytesTransferred == 0) {
        return;
    }

    data = (PUCHAR)WdfMemoryGetBuffer(Buffer, NULL);

    RingBufferWrite(&devCtx->ReadBuffer, data, (ULONG)NumBytesTransferred);

    /* Signal EV_RXCHAR event */
    SerialCompleteWaitOnMask(devCtx, SERIAL_EV_RXCHAR);

    /* Try to complete any pending read requests */
    UsbTransferProcessPendingReads(devCtx);
}

BOOLEAN
EvtUsbBulkInReadersFailed(
    _In_ WDFUSBPIPE    Pipe,
    _In_ NTSTATUS      Status,
    _In_ USBD_STATUS   UsbdStatus
    )
{
    UNREFERENCED_PARAMETER(Pipe);
    UNREFERENCED_PARAMETER(Status);
    UNREFERENCED_PARAMETER(UsbdStatus);

    /* Return TRUE to tell the framework to reset the pipe and restart */
    return TRUE;
}

/* ----------------------------------------------------------------
 * Continuous Reader Callbacks - Interrupt IN
 * ---------------------------------------------------------------- */

VOID
EvtUsbInterruptReadComplete(
    _In_ WDFUSBPIPE Pipe,
    _In_ WDFMEMORY  Buffer,
    _In_ size_t     NumBytesTransferred,
    _In_ WDFCONTEXT Context
    )
{
    PDEVICE_CONTEXT devCtx = (PDEVICE_CONTEXT)Context;
    PUCHAR          data;

    UNREFERENCED_PARAMETER(Pipe);

    if (NumBytesTransferred < 8) {
        return;
    }

    data = (PUCHAR)WdfMemoryGetBuffer(Buffer, NULL);

    /*
     * CDC ACM serial state notification format:
     *   Byte 0: bmRequestType (0xA1)
     *   Byte 1: bNotification (0x20 = SERIAL_STATE)
     *   Bytes 2-3: wValue (0)
     *   Bytes 4-5: wIndex (interface)
     *   Bytes 6-7: wLength (2)
     *   Bytes 8-9: UART state bitmap (if present)
     */
    if (data[1] == CDC_NOTIF_SERIAL_STATE && NumBytesTransferred >= 10) {
        USHORT uartState = (USHORT)(data[8] | ((USHORT)data[9] << 8));
        UsbTransferSerialStateNotify(devCtx, uartState);
    }
}

BOOLEAN
EvtUsbInterruptReadersFailed(
    _In_ WDFUSBPIPE    Pipe,
    _In_ NTSTATUS      Status,
    _In_ USBD_STATUS   UsbdStatus
    )
{
    UNREFERENCED_PARAMETER(Pipe);
    UNREFERENCED_PARAMETER(Status);
    UNREFERENCED_PARAMETER(UsbdStatus);

    return TRUE;
}

/* ----------------------------------------------------------------
 * Write Completion Routine
 * ---------------------------------------------------------------- */

VOID
EvtWriteRequestComplete(
    _In_ WDFREQUEST                     Request,
    _In_ WDFIOTARGET                    Target,
    _In_ PWDF_REQUEST_COMPLETION_PARAMS CompletionParams,
    _In_ WDFCONTEXT                     Context
    )
{
    PDEVICE_CONTEXT devCtx = (PDEVICE_CONTEXT)Context;
    NTSTATUS        status;
    size_t          bytesWritten;
    BOOLEAN         needZlp = FALSE;

    UNREFERENCED_PARAMETER(Target);

    status = CompletionParams->IoStatus.Status;
    bytesWritten = CompletionParams->Parameters.Usb.Completion->Parameters.PipeWrite.Length;

    if (NT_SUCCESS(status)) {
        devCtx->PerfStats.TransmittedCount += (ULONG)bytesWritten;

        /* Signal EV_TXEMPTY event */
        SerialCompleteWaitOnMask(devCtx, SERIAL_EV_TXEMPTY);
    }

    WdfSpinLockAcquire(devCtx->WriteLock);
    if (devCtx->PendingZlpCompleteRequest == Request) {
        devCtx->PendingZlpCompleteRequest = NULL;
        if (NT_SUCCESS(status)) {
            needZlp = TRUE;
        }
    }
    WdfSpinLockRelease(devCtx->WriteLock);

    InterlockedDecrement(&devCtx->OutstandingWrites);

    if (needZlp) {
        devCtx->ZlpBytesWritten = bytesWritten;
        WdfWorkItemEnqueue(devCtx->ZlpWorkItem);
        /* Don't complete the request yet; ZLP work item will do it */
        return;
    }

    WdfRequestCompleteWithInformation(Request, status, bytesWritten);
}

/* ----------------------------------------------------------------
 * ZLP Work Item
 * ---------------------------------------------------------------- */

VOID
EvtZlpWorkItem(
    _In_ WDFWORKITEM WorkItem
    )
{
    WDFDEVICE       device = WdfWorkItemGetParentObject(WorkItem);
    PDEVICE_CONTEXT devCtx = GetDeviceContext(device);
    NTSTATUS        status;
    WDF_MEMORY_DESCRIPTOR memDesc;
    ULONG           bytesTransferred = 0;
    UCHAR           dummy = 0;

    /*
     * Send a zero-length packet by sending a 0-byte write.
     * Some USB serial devices require this after a transfer
     * that is an exact multiple of the max packet size.
     */
    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&memDesc, &dummy, 0);

    status = WdfUsbTargetPipeWriteSynchronously(
        devCtx->BulkOutPipe,
        WDF_NO_HANDLE,
        NULL,
        &memDesc,
        &bytesTransferred
        );

    UNREFERENCED_PARAMETER(status);

    /*
     * ZLP work item is best-effort. The original write already
     * transferred the data. We intentionally don't hold a
     * reference to the original request here because the
     * write completion already handled it.
     */
}

/* ----------------------------------------------------------------
 * Helper: Process Pending Reads
 * ---------------------------------------------------------------- */

VOID
UsbTransferProcessPendingReads(
    _In_ PDEVICE_CONTEXT DevCtx
    )
{
    NTSTATUS    status;
    WDFREQUEST  request;
    PVOID       outputBuffer;
    size_t      outputLength;
    ULONG       dataAvailable;
    ULONG       bytesRead;

    for (;;) {
        dataAvailable = RingBufferGetDataLength(&DevCtx->ReadBuffer);
        if (dataAvailable == 0) {
            break;
        }

        status = WdfIoQueueRetrieveNextRequest(
            DevCtx->PendingReadQueue, &request);
        if (!NT_SUCCESS(status)) {
            break;
        }

        status = WdfRequestRetrieveOutputBuffer(
            request, 1, &outputBuffer, &outputLength);
        if (!NT_SUCCESS(status)) {
            WdfRequestCompleteWithInformation(request, status, 0);
            continue;
        }

        if ((ULONG)outputLength > dataAvailable) {
            outputLength = dataAvailable;
        }

        bytesRead = RingBufferRead(
            &DevCtx->ReadBuffer,
            (PUCHAR)outputBuffer,
            (ULONG)outputLength
            );

        DevCtx->PerfStats.ReceivedCount += bytesRead;

        WdfRequestCompleteWithInformation(request, STATUS_SUCCESS, bytesRead);
    }
}

/* ----------------------------------------------------------------
 * Helper: Serial State Notification
 * ---------------------------------------------------------------- */

VOID
UsbTransferSerialStateNotify(
    _In_ PDEVICE_CONTEXT DevCtx,
    _In_ USHORT          UartState
    )
{
    ULONG oldModem;
    ULONG newModem = 0;
    ULONG events   = 0;

    /* Map CDC serial state bits to MSR bits */
    if (UartState & CDC_STATE_DCD) newModem |= SERIAL_MSR_DCD;
    if (UartState & CDC_STATE_DSR) newModem |= SERIAL_MSR_DSR;
    if (UartState & CDC_STATE_RI)  newModem |= SERIAL_MSR_RI;

    /* Track delta bits */
    oldModem = DevCtx->ModemStatus;

    if ((oldModem ^ newModem) & SERIAL_MSR_DCD) {
        newModem |= SERIAL_MSR_DDCD;
        events |= SERIAL_EV_RLSD;
    }
    if ((oldModem ^ newModem) & SERIAL_MSR_DSR) {
        newModem |= SERIAL_MSR_DDSR;
        events |= SERIAL_EV_DSR;
    }
    if ((oldModem ^ newModem) & SERIAL_MSR_CTS) {
        newModem |= SERIAL_MSR_DCTS;
        events |= SERIAL_EV_CTS;
    }
    if ((newModem & SERIAL_MSR_RI) && !(oldModem & SERIAL_MSR_RI)) {
        newModem |= SERIAL_MSR_TERI;
        events |= SERIAL_EV_RING;
    }

    DevCtx->ModemStatus = newModem;

    /* Check error state bits */
    if (UartState & CDC_STATE_BREAK) {
        events |= SERIAL_EV_BREAK;
    }
    if (UartState & CDC_STATE_FRAMING) {
        DevCtx->PerfStats.FrameErrorCount++;
        events |= SERIAL_EV_ERR;
    }
    if (UartState & CDC_STATE_PARITY) {
        DevCtx->PerfStats.ParityErrorCount++;
        events |= SERIAL_EV_ERR;
    }
    if (UartState & CDC_STATE_OVERRUN) {
        DevCtx->PerfStats.BufferOverrunErrorCount++;
        events |= SERIAL_EV_ERR;
    }

    if (events != 0) {
        SerialCompleteWaitOnMask(DevCtx, events);
    }
}
