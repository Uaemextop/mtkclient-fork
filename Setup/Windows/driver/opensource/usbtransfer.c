/*
 * usbtransfer.c
 *
 * USB data transfer engine: ring buffer, continuous reader callbacks
 * for Bulk IN and Interrupt IN, write completion with ZLP support.
 *
 * Equivalent to the original Data_* functions (section 9) and
 * interrupt notification handler (section 10).
 *
 * SPDX-License-Identifier: MIT
 */

#include "mtk_usb2ser.h"

/* =========================================================================
 *  Ring Buffer Implementation
 *  Equivalent to the original BUFFER_MANAGER / Data_FillinDataFromBuffer
 * ========================================================================= */

NTSTATUS
RingBufferInit(
    _In_ PRING_BUFFER Rb,
    _In_ ULONG        Size
    )
{
    Rb->Buffer = (PUCHAR)ExAllocatePool2(
        POOL_FLAG_NON_PAGED, Size, MTK_POOL_TAG);
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
    Rb->Size       = 0;
    Rb->ReadOffset = 0;
    Rb->WriteOffset = 0;
    Rb->DataLength = 0;
}

/*
 * Write data into the ring buffer.
 * Returns the number of bytes actually written (may be less than Length
 * if the buffer is full).
 */
ULONG
RingBufferWrite(
    _In_                    PRING_BUFFER Rb,
    _In_reads_(Length)      PUCHAR       Data,
    _In_                    ULONG        Length
    )
{
    ULONG   spaceAvailable;
    ULONG   bytesToWrite;
    ULONG   firstChunk;
    ULONG   secondChunk;

    WdfSpinLockAcquire(Rb->Lock);

    spaceAvailable = Rb->Size - Rb->DataLength;
    bytesToWrite   = min(Length, spaceAvailable);

    if (bytesToWrite == 0) {
        WdfSpinLockRelease(Rb->Lock);
        return 0;
    }

    /* Write in up to two segments (wrap-around) */
    firstChunk = min(bytesToWrite, Rb->Size - Rb->WriteOffset);
    RtlCopyMemory(Rb->Buffer + Rb->WriteOffset, Data, firstChunk);

    secondChunk = bytesToWrite - firstChunk;
    if (secondChunk > 0) {
        RtlCopyMemory(Rb->Buffer, Data + firstChunk, secondChunk);
    }

    Rb->WriteOffset = (Rb->WriteOffset + bytesToWrite) % Rb->Size;
    Rb->DataLength += bytesToWrite;

    WdfSpinLockRelease(Rb->Lock);

    return bytesToWrite;
}

/*
 * Read data from the ring buffer.
 * Returns the number of bytes actually read (may be less than Length
 * if not enough data is available).
 */
ULONG
RingBufferRead(
    _In_                    PRING_BUFFER Rb,
    _Out_writes_(Length)    PUCHAR       Data,
    _In_                    ULONG        Length
    )
{
    ULONG   bytesToRead;
    ULONG   firstChunk;
    ULONG   secondChunk;

    WdfSpinLockAcquire(Rb->Lock);

    bytesToRead = min(Length, Rb->DataLength);

    if (bytesToRead == 0) {
        WdfSpinLockRelease(Rb->Lock);
        return 0;
    }

    /* Read in up to two segments (wrap-around) */
    firstChunk = min(bytesToRead, Rb->Size - Rb->ReadOffset);
    RtlCopyMemory(Data, Rb->Buffer + Rb->ReadOffset, firstChunk);

    secondChunk = bytesToRead - firstChunk;
    if (secondChunk > 0) {
        RtlCopyMemory(Data + firstChunk, Rb->Buffer, secondChunk);
    }

    Rb->ReadOffset = (Rb->ReadOffset + bytesToRead) % Rb->Size;
    Rb->DataLength -= bytesToRead;

    WdfSpinLockRelease(Rb->Lock);

    return bytesToRead;
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

/* =========================================================================
 *  UsbTransferProcessPendingReads
 *  Equivalent to the original Data_InDpcCallback (section 9)
 *
 *  Drains the pending-read queue by filling requests from the ring buffer.
 *  Called after new data is added to the ring buffer.
 * ========================================================================= */
VOID
UsbTransferProcessPendingReads(
    _In_ PDEVICE_CONTEXT DevCtx
    )
{
    NTSTATUS    status;
    WDFREQUEST  readRequest;

    for (;;) {
        status = WdfIoQueueRetrieveNextRequest(
            DevCtx->PendingReadQueue, &readRequest);
        if (!NT_SUCCESS(status)) {
            /* No more pending reads */
            break;
        }

        {
            PVOID   buffer;
            size_t  bufferLength;
            ULONG   bytesRead;

            status = WdfRequestRetrieveOutputBuffer(
                readRequest, 1, &buffer, &bufferLength);
            if (!NT_SUCCESS(status)) {
                WdfRequestComplete(readRequest, status);
                continue;
            }

            bytesRead = RingBufferRead(
                &DevCtx->ReadBuffer,
                (PUCHAR)buffer,
                (ULONG)bufferLength
                );

            if (bytesRead > 0) {
                DevCtx->PerfStats.ReceivedCount += bytesRead;
                WdfRequestCompleteWithInformation(
                    readRequest, STATUS_SUCCESS, bytesRead);
            } else {
                /*
                 * No data available — put the request back.
                 * Use WdfRequestRequeue to put it back at the head.
                 */
                status = WdfRequestRequeue(readRequest);
                if (!NT_SUCCESS(status)) {
                    WdfRequestComplete(readRequest, status);
                }
                break;
            }
        }
    }
}

/* =========================================================================
 *  EvtUsbBulkInReadComplete — continuous reader callback for Bulk IN
 *  Equivalent to the original Data_InComplete + Data_InTransReorder (section 9)
 *
 *  Called by KMDF when data arrives from the USB bulk IN endpoint.
 *  KMDF handles:
 *    - Multiple outstanding URBs (via NumPendingReads)
 *    - In-order delivery (serialised callback invocation)
 *    - Error recovery and pipe reset
 *
 *  This eliminates the original driver's manual URB reordering logic
 *  (Data_InTransReorder / InSequenceNumber / NextExpectedSequence).
 * ========================================================================= */
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
    ULONG           bytesWritten;

    UNREFERENCED_PARAMETER(Pipe);

    if (NumBytesTransferred == 0) {
        return;
    }

    data = (PUCHAR)WdfMemoryGetBuffer(Buffer, NULL);

    /* Write received data into the ring buffer */
    bytesWritten = RingBufferWrite(
        &devCtx->ReadBuffer,
        data,
        (ULONG)NumBytesTransferred
        );

    if (bytesWritten < (ULONG)NumBytesTransferred) {
        /* Ring buffer overflow — count as buffer overrun */
        devCtx->PerfStats.BufferOverrunErrorCount++;
    }

    /* Signal SERIAL_EV_RXCHAR event */
    SerialCompleteWaitOnMask(devCtx, SERIAL_EV_RXCHAR);

    /* Check for EventChar (SERIAL_EV_RXFLAG) */
    if (devCtx->SpecialChars.EventChar != 0) {
        PUCHAR p = data;
        ULONG  i;
        for (i = 0; i < (ULONG)NumBytesTransferred; i++) {
            if (p[i] == devCtx->SpecialChars.EventChar) {
                SerialCompleteWaitOnMask(devCtx, SERIAL_EV_RXFLAG);
                break;
            }
        }
    }

    /* Try to complete any pending read requests from the ring buffer */
    UsbTransferProcessPendingReads(devCtx);

    /*
     * Re-arm the read-interval timer.
     * This resets the inter-character gap countdown after each USB
     * bulk-IN completion, so the timer fires ReadIntervalTimeout ms
     * after the *last* byte of this transfer — matching Win32 semantics.
     *
     * ReadIntervalTimerArm is a no-op if ReadIntervalTimeout == 0 or
     * MAXULONG (those cases are handled directly in EvtIoRead).
     */
    ReadIntervalTimerArm(devCtx);
}

/* =========================================================================
 *  EvtUsbBulkInReadersFailed — error callback for Bulk IN continuous reader
 * ========================================================================= */
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

    /*
     * Return TRUE to have the framework reset the pipe and restart
     * the continuous reader. This matches the original driver's
     * Data_StartResetInPipe error recovery.
     */
    return TRUE;
}

/* =========================================================================
 *  EvtUsbInterruptReadComplete — continuous reader callback for Interrupt IN
 *  Equivalent to the original Data_IntrUrbComplete / Data_SerialStateNotify
 *  (section 10)
 *
 *  Processes CDC ACM SERIAL_STATE notifications:
 *    Byte 0: bmRequestType (0xA1)
 *    Byte 1: bNotification (0x20 = SERIAL_STATE)
 *    Bytes 2-3: wValue (0)
 *    Bytes 4-5: wIndex (interface)
 *    Bytes 6-7: wLength (2)
 *    Bytes 8-9: UART state bitmap
 * ========================================================================= */
VOID
EvtUsbInterruptReadComplete(
    _In_ WDFUSBPIPE Pipe,
    _In_ WDFMEMORY  Buffer,
    _In_ size_t     NumBytesTransferred,
    _In_ WDFCONTEXT Context
    )
{
    PDEVICE_CONTEXT devCtx = (PDEVICE_CONTEXT)Context;
    PUCHAR          notifData;

    UNREFERENCED_PARAMETER(Pipe);

    if (NumBytesTransferred < 10) {
        return;
    }

    notifData = (PUCHAR)WdfMemoryGetBuffer(Buffer, NULL);

    /* Verify this is a SERIAL_STATE notification (bNotification = 0x20) */
    if (notifData[1] != CDC_NOTIF_SERIAL_STATE) {
        return;
    }

    {
        USHORT uartState = *(PUSHORT)(notifData + 8);
        UsbTransferSerialStateNotify(devCtx, uartState);
    }
}

/* =========================================================================
 *  EvtUsbInterruptReadersFailed — error callback for Interrupt IN reader
 * ========================================================================= */
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

    /* Reset and restart the interrupt pipe reader */
    return TRUE;
}

/* =========================================================================
 *  UsbTransferSerialStateNotify — process CDC SERIAL_STATE notification
 *  Equivalent to the original Data_SerialStateNotify (section 10)
 * ========================================================================= */
VOID
UsbTransferSerialStateNotify(
    _In_ PDEVICE_CONTEXT DevCtx,
    _In_ USHORT          UartState
    )
{
    ULONG   newModemStatus = 0;
    ULONG   prevModemStatus;
    ULONG   events = 0;

    /* Map CDC ACM state bits to Win32 modem status register bits */
    if (UartState & CDC_STATE_DCD) {
        newModemStatus |= SERIAL_MSR_DCD;
    }
    if (UartState & CDC_STATE_DSR) {
        newModemStatus |= SERIAL_MSR_DSR;
    }
    if (UartState & CDC_STATE_RI) {
        newModemStatus |= SERIAL_MSR_RI;
    }
    /* CTS is always asserted for CDC ACM (virtual port) */
    newModemStatus |= SERIAL_MSR_CTS;

    prevModemStatus = DevCtx->ModemStatus;
    DevCtx->ModemStatus = newModemStatus;

    /* Detect changes for event notification */
    if ((newModemStatus ^ prevModemStatus) & SERIAL_MSR_DCD) {
        events |= SERIAL_EV_RLSD;
    }
    if ((newModemStatus ^ prevModemStatus) & SERIAL_MSR_DSR) {
        events |= SERIAL_EV_DSR;
    }
    if ((newModemStatus ^ prevModemStatus) & SERIAL_MSR_CTS) {
        events |= SERIAL_EV_CTS;
    }
    if ((newModemStatus ^ prevModemStatus) & SERIAL_MSR_RI) {
        events |= SERIAL_EV_RING;
    }

    /* Break detection */
    if (UartState & CDC_STATE_BREAK) {
        events |= SERIAL_EV_BREAK;
    }

    /* Error detection */
    if (UartState & (CDC_STATE_FRAMING | CDC_STATE_PARITY | CDC_STATE_OVERRUN)) {
        events |= SERIAL_EV_ERR;

        if (UartState & CDC_STATE_FRAMING) {
            DevCtx->PerfStats.FrameErrorCount++;
        }
        if (UartState & CDC_STATE_PARITY) {
            DevCtx->PerfStats.ParityErrorCount++;
        }
        if (UartState & CDC_STATE_OVERRUN) {
            DevCtx->PerfStats.SerialOverrunErrorCount++;
        }
    }

    /* Complete pending WAIT_ON_MASK if events match */
    if (events != 0) {
        SerialCompleteWaitOnMask(DevCtx, events);
    }
}

/* =========================================================================
 *  EvtWriteRequestComplete — completion routine for bulk OUT writes
 *  Equivalent to the original Data_OutComplete (section 9)
 *
 *  Handles:
 *    - Write completion with byte count
 *    - ZLP (Zero Length Packet) for transfers that are exact multiples of
 *      the max packet size (512 for USB 2.0 HS)
 *    - SERIAL_EV_TXEMPTY event notification
 * ========================================================================= */
VOID
EvtWriteRequestComplete(
    _In_ WDFREQUEST                     Request,
    _In_ WDFIOTARGET                    Target,
    _In_ PWDF_REQUEST_COMPLETION_PARAMS Params,
    _In_ WDFCONTEXT                     Context
    )
{
    PDEVICE_CONTEXT     devCtx = (PDEVICE_CONTEXT)Context;
    NTSTATUS            status = Params->IoStatus.Status;
    size_t              bytesWritten = Params->IoStatus.Information;

    UNREFERENCED_PARAMETER(Target);

    InterlockedDecrement(&devCtx->OutstandingWrites);

    if (NT_SUCCESS(status)) {
        devCtx->PerfStats.TransmittedCount += (ULONG)bytesWritten;

        /*
         * Check if ZLP is needed:
         * If the write was an exact multiple of max packet size,
         * send a zero-length packet to signal transfer boundary.
         * This is required for CDC ACM protocol compliance.
         */
        if (bytesWritten > 0 &&
            devCtx->BulkOutMaxPacket > 0 &&
            (bytesWritten % devCtx->BulkOutMaxPacket) == 0) {
            /*
             * Queue a work item to send ZLP at PASSIVE_LEVEL.
             * Store the request so we can complete it after ZLP.
             */
            devCtx->PendingZlpCompleteRequest = Request;
            devCtx->ZlpBytesWritten = bytesWritten;
            WdfWorkItemEnqueue(devCtx->ZlpWorkItem);
            return;
        }
    }

    /* Signal SERIAL_EV_TXEMPTY when all writes complete */
    if (devCtx->OutstandingWrites == 0) {
        SerialCompleteWaitOnMask(devCtx, SERIAL_EV_TXEMPTY);
    }

    WdfRequestCompleteWithInformation(Request, status, bytesWritten);
}

/* =========================================================================
 *  EvtZlpWorkItem — send Zero Length Packet at PASSIVE_LEVEL
 *  Equivalent to the original Data_OutSendZLP (section 9)
 * ========================================================================= */
VOID
EvtZlpWorkItem(
    _In_ WDFWORKITEM WorkItem
    )
{
    WDFDEVICE           device = WdfWorkItemGetParentObject(WorkItem);
    PDEVICE_CONTEXT     devCtx = GetDeviceContext(device);
    WDF_MEMORY_DESCRIPTOR memDesc;
    WDFREQUEST          request;
    ULONG_PTR           bytesWritten;

    request      = devCtx->PendingZlpCompleteRequest;
    bytesWritten = devCtx->ZlpBytesWritten;

    devCtx->PendingZlpCompleteRequest = NULL;
    devCtx->ZlpBytesWritten           = 0;

    if (request == NULL) {
        return;
    }

    /*
     * Send a zero-length bulk OUT transfer.
     * Use a small dummy buffer with length 0.
     */
    {
        UCHAR dummy = 0;
        WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&memDesc, &dummy, 0);

        /* Synchronous zero-length write — we're at PASSIVE_LEVEL */
        WdfUsbTargetPipeWriteSynchronously(
            devCtx->BulkOutPipe,
            NULL,
            NULL,
            &memDesc,
            NULL
            );
    }

    /* Signal SERIAL_EV_TXEMPTY */
    if (devCtx->OutstandingWrites == 0) {
        SerialCompleteWaitOnMask(devCtx, SERIAL_EV_TXEMPTY);
    }

    WdfRequestCompleteWithInformation(request, STATUS_SUCCESS, bytesWritten);
}
