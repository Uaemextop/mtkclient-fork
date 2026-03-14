/*
 * device.c
 *
 * PnP/Power lifecycle, USB configuration, pipe setup, symbolic link
 * management, and file object (Create/Close/Cleanup) handlers.
 *
 * SPDX-License-Identifier: MIT
 */

#include "mtk_usb2ser.h"

/* Registry path for stable COM port map, indexed by USB serial number.
 *
 * When a MTK device switches mode (Preloader PID_2000 → DA PID_2001) it
 * re-enumerates under a different hardware ID and Windows would normally
 * assign it a brand-new COM port number.  We avoid this by storing the
 * last-used port name in a driver-managed key under this path, keyed by
 * the USB serial number string.
 *
 * Key:  HKLM\SOFTWARE\MediaTek\USBPortMap\<iSerialNumber>
 * Value: REG_SZ "COM3" (or whatever was assigned)
 */
#define MTK_PORT_MAP_REGKEY  L"\\Registry\\Machine\\SOFTWARE\\MediaTek\\USBPortMap"

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, EvtDevicePrepareHardware)
#pragma alloc_text(PAGE, EvtDeviceReleaseHardware)
#pragma alloc_text(PAGE, EvtDeviceD0Entry)
#pragma alloc_text(PAGE, EvtDeviceD0Exit)
#pragma alloc_text(PAGE, EvtDeviceSurpriseRemoval)
#pragma alloc_text(PAGE, EvtDeviceFileCreate)
#pragma alloc_text(PAGE, EvtFileClose)
#pragma alloc_text(PAGE, EvtFileCleanup)
#pragma alloc_text(PAGE, DeviceConfigureUsbDevice)
#pragma alloc_text(PAGE, DeviceConfigureUsbPipes)
#pragma alloc_text(PAGE, DeviceCreateSymbolicLink)
#pragma alloc_text(PAGE, DeviceRemoveSymbolicLink)
#pragma alloc_text(PAGE, DeviceReadRegistrySettings)
#endif

/* =========================================================================
 *  EvtDevicePrepareHardware
 *  Equivalent to the original PnP_StartDevice (section 5)
 * ========================================================================= */
NTSTATUS
EvtDevicePrepareHardware(
    _In_ WDFDEVICE    Device,
    _In_ WDFCMRESLIST ResourcesRaw,
    _In_ WDFCMRESLIST ResourcesTranslated
    )
{
    NTSTATUS        status;
    PDEVICE_CONTEXT devCtx = GetDeviceContext(Device);

    PAGED_CODE();
    UNREFERENCED_PARAMETER(ResourcesRaw);
    UNREFERENCED_PARAMETER(ResourcesTranslated);

    /* Create and configure the USB device object */
    status = DeviceConfigureUsbDevice(devCtx);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    /* Enumerate and configure USB pipes (Bulk IN, Bulk OUT, Interrupt IN) */
    status = DeviceConfigureUsbPipes(devCtx);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    /* Initialise the ring buffer for incoming serial data */
    status = RingBufferInit(&devCtx->ReadBuffer, DEFAULT_READ_BUFFER_SIZE);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    /* Read registry settings (IdleTime, IdleEnable, PortName, etc.) */
    DeviceReadRegistrySettings(devCtx);

    /* Create COM port symbolic link and SERIALCOMM entry */
    status = DeviceCreateSymbolicLink(devCtx);
    if (!NT_SUCCESS(status)) {
        /* Non-fatal: device can still work via device interface */
    }

    /* Configure selective suspend / idle power policy */
    PowerConfigureIdleSettings(devCtx);

    /*
     * Create the read-interval timeout timer.
     *
     * This one-shot timer is armed by EvtUsbBulkInReadComplete after data
     * arrives.  When it fires (ReadIntervalTimeout ms after the last byte),
     * it completes all pending read requests with whatever data is in the
     * ring buffer — even if they asked for more bytes.
     *
     * This implements the Win32 ReadIntervalTimeout semantics:
     *   "The maximum time allowed to elapse between the arrival of two
     *    characters on the communications line."
     *
     * mtkclient's pyserial backend configures this timeout (20 ms default)
     * and expects reads to return promptly once the inter-character gap is
     * detected.  Without this timer, reads with a finite interval timeout
     * would block until the full requested byte count is received.
     */
    {
        WDF_TIMER_CONFIG    timerConfig;
        WDF_OBJECT_ATTRIBUTES timerAttrs;

        WDF_TIMER_CONFIG_INIT(&timerConfig, EvtReadIntervalTimer);
        timerConfig.AutomaticSerialization = FALSE;
        timerConfig.Period                 = 0;   /* One-shot */
        timerConfig.TolerableDelay         = 0;

        WDF_OBJECT_ATTRIBUTES_INIT(&timerAttrs);
        timerAttrs.ParentObject = Device;

        status = WdfTimerCreate(&timerConfig, &timerAttrs,
                                &devCtx->ReadIntervalTimer);
        if (!NT_SUCCESS(status)) {
            return status;
        }

        status = WdfSpinLockCreate(WDF_NO_OBJECT_ATTRIBUTES,
                                   &devCtx->ReadTimerLock);
        if (!NT_SUCCESS(status)) {
            return status;
        }

        devCtx->ReadTimerArmed = FALSE;
    }

    devCtx->DeviceStarted = TRUE;

    return STATUS_SUCCESS;
}

/* =========================================================================
 *  EvtDeviceReleaseHardware
 *  Equivalent to the original PnP_RemoveDevice (section 5)
 * ========================================================================= */
NTSTATUS
EvtDeviceReleaseHardware(
    _In_ WDFDEVICE    Device,
    _In_ WDFCMRESLIST ResourcesTranslated
    )
{
    PDEVICE_CONTEXT devCtx = GetDeviceContext(Device);

    PAGED_CODE();
    UNREFERENCED_PARAMETER(ResourcesTranslated);

    devCtx->DeviceStarted = FALSE;

    /* Remove symbolic link and SERIALCOMM entry */
    DeviceRemoveSymbolicLink(devCtx);

    /* Free ring buffer */
    RingBufferFree(&devCtx->ReadBuffer);

    return STATUS_SUCCESS;
}

/* =========================================================================
 *  EvtDeviceD0Entry — device enters working state
 * ========================================================================= */
NTSTATUS
EvtDeviceD0Entry(
    _In_ WDFDEVICE              Device,
    _In_ WDF_POWER_DEVICE_STATE PreviousState
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(PreviousState);

    /*
     * The WDF continuous readers (bulk IN and interrupt) are automatically
     * started by the framework when the device enters D0.
     */

    return STATUS_SUCCESS;
}

/* =========================================================================
 *  EvtDeviceD0Exit — device leaves working state
 * ========================================================================= */
NTSTATUS
EvtDeviceD0Exit(
    _In_ WDFDEVICE              Device,
    _In_ WDF_POWER_DEVICE_STATE TargetState
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(TargetState);

    /*
     * The WDF continuous readers are automatically stopped by the
     * framework when the device leaves D0.
     */

    return STATUS_SUCCESS;
}

/* =========================================================================
 *  EvtDeviceSurpriseRemoval — device unexpectedly disconnected
 *
 *  This fires when the MTK device re-enumerates during a mode switch
 *  (e.g. Preloader → DA) or when the user physically unplugs the cable.
 *
 *  We must:
 *  1. Mark the device as no longer started so new I/O fails immediately.
 *  2. Stop the read-interval timer to avoid a timer callback racing with
 *     device teardown.
 *  3. Purge all manual queues so pending read/wait requests are cancelled
 *     and delivered to userspace (mtkclient) right away.
 *
 *  Without this, mtkclient would time out waiting for reads to complete,
 *  adding several seconds of dead time during every mode switch.
 * ========================================================================= */
VOID
EvtDeviceSurpriseRemoval(
    _In_ WDFDEVICE Device
    )
{
    PDEVICE_CONTEXT devCtx = GetDeviceContext(Device);

    PAGED_CODE();

    /* Prevent new I/O from starting */
    devCtx->DeviceStarted = FALSE;
    devCtx->DeviceOpen    = FALSE;

    /* Stop the read-interval timer (if armed) so it doesn't fire after
     * the device context has been partially torn down. */
    WdfTimerStop(devCtx->ReadIntervalTimer, TRUE /* wait */);

    WdfSpinLockAcquire(devCtx->ReadTimerLock);
    devCtx->ReadTimerArmed = FALSE;
    WdfSpinLockRelease(devCtx->ReadTimerLock);

    /* Purge pending read requests — they complete with STATUS_CANCELLED,
     * which pyserial/mtkclient interprets as the device going away. */
    WdfIoQueuePurgeSynchronously(devCtx->PendingReadQueue);
    WdfIoQueueStart(devCtx->PendingReadQueue);

    /* Purge pending WAIT_ON_MASK requests */
    WdfIoQueuePurgeSynchronously(devCtx->PendingWaitMaskQueue);
    WdfIoQueueStart(devCtx->PendingWaitMaskQueue);

    /* Purge the ring buffer */
    RingBufferPurge(&devCtx->ReadBuffer);
}

/* =========================================================================
 *  EvtDeviceFileCreate — handle opened on our device
 *  Equivalent to original FileOp_Create (section 6)
 * ========================================================================= */
VOID
EvtDeviceFileCreate(
    _In_ WDFDEVICE  Device,
    _In_ WDFREQUEST Request,
    _In_ WDFFILEOBJECT FileObject
    )
{
    PDEVICE_CONTEXT devCtx = GetDeviceContext(Device);
    NTSTATUS        status;

    PAGED_CODE();
    UNREFERENCED_PARAMETER(FileObject);

    if (!devCtx->DeviceStarted) {
        WdfRequestComplete(Request, STATUS_DEVICE_NOT_READY);
        return;
    }

    /* Purge any stale data in the ring buffer */
    RingBufferPurge(&devCtx->ReadBuffer);

    /* Reset performance statistics */
    RtlZeroMemory(&devCtx->PerfStats, sizeof(SERIALPERF_STATS));

    /* Set DTR and RTS (via CDC SET_CONTROL_LINE_STATE) */
    UsbControlSetDtr(devCtx);
    UsbControlSetRts(devCtx);

    /* Apply current line coding (baud, data bits, parity, stop bits) */
    status = UsbControlSetLineCoding(devCtx);
    /* Non-fatal if this fails — some devices may not implement it */

    UNREFERENCED_PARAMETER(status);

    devCtx->DeviceOpen = TRUE;

    WdfRequestComplete(Request, STATUS_SUCCESS);
}

/* =========================================================================
 *  EvtFileClose — last handle closed
 *  Equivalent to original FileOp_Close (section 6)
 * ========================================================================= */
VOID
EvtFileClose(
    _In_ WDFFILEOBJECT FileObject
    )
{
    WDFDEVICE       device = WdfFileObjectGetDevice(FileObject);
    PDEVICE_CONTEXT devCtx = GetDeviceContext(device);

    PAGED_CODE();

    devCtx->DeviceOpen = FALSE;

    /* Clear DTR and RTS */
    UsbControlClrDtr(devCtx);
    UsbControlClrRts(devCtx);
}

/* =========================================================================
 *  EvtFileCleanup — handle being cleaned up (cancel pending I/O)
 * ========================================================================= */
VOID
EvtFileCleanup(
    _In_ WDFFILEOBJECT FileObject
    )
{
    WDFDEVICE       device = WdfFileObjectGetDevice(FileObject);
    PDEVICE_CONTEXT devCtx = GetDeviceContext(device);

    PAGED_CODE();

    /* Drain and purge all manual queues */
    WdfIoQueuePurgeSynchronously(devCtx->PendingReadQueue);
    WdfIoQueuePurgeSynchronously(devCtx->PendingWaitMaskQueue);

    /* Restart the queues so they are ready for the next open */
    WdfIoQueueStart(devCtx->PendingReadQueue);
    WdfIoQueueStart(devCtx->PendingWaitMaskQueue);

    /* Purge ring buffer */
    RingBufferPurge(&devCtx->ReadBuffer);

    /* Clear event history and wait mask */
    WdfSpinLockAcquire(devCtx->EventLock);
    devCtx->WaitMask    = 0;
    devCtx->EventHistory = 0;
    WdfSpinLockRelease(devCtx->EventLock);
}

/* =========================================================================
 *  DeviceConfigureUsbDevice — create USB target device and select config
 * ========================================================================= */
NTSTATUS
DeviceConfigureUsbDevice(
    _In_ PDEVICE_CONTEXT DevCtx
    )
{
    NTSTATUS                            status;
    WDF_USB_DEVICE_CREATE_CONFIG        usbConfig;
    WDF_USB_DEVICE_SELECT_CONFIG_PARAMS configParams;

    PAGED_CODE();

    WDF_USB_DEVICE_CREATE_CONFIG_INIT(&usbConfig,
                                       USBD_CLIENT_CONTRACT_VERSION_602);

    status = WdfUsbTargetDeviceCreateWithParameters(
        DevCtx->Device,
        &usbConfig,
        WDF_NO_OBJECT_ATTRIBUTES,
        &DevCtx->UsbDevice
        );
    if (!NT_SUCCESS(status)) {
        return status;
    }

    /*
     * Select default configuration. Use MULTIPLE_INTERFACES to handle
     * CDC ACM devices that expose separate Communication and Data interfaces.
     *
     * For composite interface nodes (e.g. Meta/ETS/Modem PIDs with MI_xx),
     * usbccgp.sys has already selected the configuration.  In that case
     * WdfUsbTargetDeviceSelectConfig returns STATUS_INVALID_DEVICE_STATE,
     * which we treat as success and continue with pipe enumeration.
     */
    WDF_USB_DEVICE_SELECT_CONFIG_PARAMS_INIT_MULTIPLE_INTERFACES(
        &configParams,
        0,
        NULL
        );

    status = WdfUsbTargetDeviceSelectConfig(
        DevCtx->UsbDevice,
        WDF_NO_OBJECT_ATTRIBUTES,
        &configParams
        );

    if (status == STATUS_INVALID_DEVICE_STATE) {
        /* Composite device — already configured by usbccgp.sys, proceed. */
        status = STATUS_SUCCESS;
    }

    if (!NT_SUCCESS(status)) {
        return status;
    }

    /*
     * Read the USB iSerialNumber string descriptor.
     *
     * This is used in DeviceCreateSymbolicLink to provide stable COM port
     * assignment: two enumerations of the same physical device (e.g. one
     * in Preloader mode and one in DA mode) will share the same USB serial
     * number string and therefore be mapped to the same COMx port.
     *
     * Failure is non-fatal: if no serial number is available (or the device
     * doesn't implement iSerialNumber) we fall back to the normal MsPorts
     * assignment.
     */
    {
        USB_DEVICE_DESCRIPTOR   devDesc;

        WdfUsbTargetDeviceGetDeviceDescriptor(DevCtx->UsbDevice, &devDesc);

        if (devDesc.iSerialNumber != 0) {
            WCHAR   serBuf[64];
            /*
             * WdfUsbTargetDeviceQueryString uses CHARACTER count, not bytes.
             * Pass the buffer size in WCHARs; on return it holds the number
             * of characters written (not including null-terminator).
             */
            USHORT  serBufChars = (USHORT)(sizeof(serBuf) / sizeof(WCHAR));
            NTSTATUS srStatus;

            RtlZeroMemory(serBuf, sizeof(serBuf));

            srStatus = WdfUsbTargetDeviceQueryString(
                DevCtx->UsbDevice,
                NULL,               /* no request — synchronous internal */
                NULL,               /* no send options */
                serBuf,
                &serBufChars,
                devDesc.iSerialNumber,
                0x0409              /* English */
                );

            if (NT_SUCCESS(srStatus) && serBufChars > 0) {
                USHORT copyBytes = (USHORT)(serBufChars * sizeof(WCHAR));
                if (copyBytes > sizeof(DevCtx->UsbSerialBuf) - sizeof(WCHAR)) {
                    copyBytes = (USHORT)(sizeof(DevCtx->UsbSerialBuf) - sizeof(WCHAR));
                }
                RtlCopyMemory(DevCtx->UsbSerialBuf, serBuf, copyBytes);
                DevCtx->UsbSerialLen = copyBytes;
            }
        }
    }

    return STATUS_SUCCESS;
}

/* =========================================================================
 *  DeviceConfigureUsbPipes — find and configure Bulk IN/OUT and Interrupt
 *  Equivalent to the original Data_GetDataConfig (section 9)
 * ========================================================================= */
NTSTATUS
DeviceConfigureUsbPipes(
    _In_ PDEVICE_CONTEXT DevCtx
    )
{
    NTSTATUS                    status = STATUS_SUCCESS;
    BYTE                        numInterfaces;
    BYTE                        interfaceIdx, pipeIdx;
    WDF_USB_PIPE_INFORMATION    pipeInfo;
    /*
     * CommInterfaceNumber tracks the interface that hosts the interrupt
     * (CDC communication) endpoint.  This is the correct wIndex value for
     * CDC class-specific control requests (SET_LINE_CODING, etc.).
     * It is set to 0xFF until a communication interface is found.
     */
    UCHAR                       commIfaceNumber = 0xFF;

    PAGED_CODE();

    DevCtx->BulkInPipe    = NULL;
    DevCtx->BulkOutPipe   = NULL;
    DevCtx->InterruptPipe = NULL;

    numInterfaces = WdfUsbTargetDeviceGetNumInterfaces(DevCtx->UsbDevice);

    for (interfaceIdx = 0; interfaceIdx < numInterfaces; interfaceIdx++) {
        WDFUSBINTERFACE usbInterface;
        BYTE            numPipes;

        usbInterface = WdfUsbTargetDeviceGetInterface(
            DevCtx->UsbDevice, interfaceIdx);
        numPipes = WdfUsbInterfaceGetNumConfiguredPipes(usbInterface);

        for (pipeIdx = 0; pipeIdx < numPipes; pipeIdx++) {
            WDFUSBPIPE pipe;

            WDF_USB_PIPE_INFORMATION_INIT(&pipeInfo);
            pipe = WdfUsbInterfaceGetConfiguredPipe(
                usbInterface, pipeIdx, &pipeInfo);

            if (pipeInfo.PipeType == WdfUsbPipeTypeBulk) {
                if (USB_ENDPOINT_DIRECTION_IN(pipeInfo.EndpointAddress)) {
                    if (DevCtx->BulkInPipe == NULL) {
                        DevCtx->BulkInPipe       = pipe;
                        DevCtx->BulkInMaxPacket  = pipeInfo.MaximumPacketSize;
                        DevCtx->UsbInterface     = usbInterface;
                    }
                } else {
                    if (DevCtx->BulkOutPipe == NULL) {
                        DevCtx->BulkOutPipe      = pipe;
                        DevCtx->BulkOutMaxPacket = pipeInfo.MaximumPacketSize;
                    }
                }
            } else if (pipeInfo.PipeType == WdfUsbPipeTypeInterrupt) {
                if (USB_ENDPOINT_DIRECTION_IN(pipeInfo.EndpointAddress)) {
                    if (DevCtx->InterruptPipe == NULL) {
                        DevCtx->InterruptPipe = pipe;
                        /*
                         * Record the communication interface number (the
                         * interface that has the interrupt/notification
                         * endpoint).  This is used as wIndex in CDC
                         * class-specific control requests.
                         */
                        commIfaceNumber =
                            WdfUsbInterfaceGetInterfaceNumber(usbInterface);
                    }
                }
            }
        }
    }

    if (DevCtx->BulkInPipe == NULL || DevCtx->BulkOutPipe == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /*
     * Determine the interface number for CDC control requests (wIndex).
     *
     * Priority:
     *   1. Communication interface (interrupt pipe) — the correct wIndex for
     *      standard two-interface CDC ACM (comm = 0, data = 1) or for
     *      composite devices where comm = MI_N, data = MI_{N+1}.
     *   2. Data interface (bulk in pipe) — used only for single-interface
     *      devices (e.g. BROM PID 0x0003) that have no interrupt endpoint.
     *      Since the only interface IS interface 0, wIndex=0 is correct.
     */
    if (commIfaceNumber != 0xFF) {
        DevCtx->InterfaceNumber = commIfaceNumber;
    } else if (DevCtx->UsbInterface != NULL) {
        DevCtx->InterfaceNumber =
            WdfUsbInterfaceGetInterfaceNumber(DevCtx->UsbInterface);
    }

    /* Disable short-packet check for the bulk pipes */
    WdfUsbTargetPipeSetNoMaximumPacketSizeCheck(DevCtx->BulkInPipe);
    WdfUsbTargetPipeSetNoMaximumPacketSizeCheck(DevCtx->BulkOutPipe);

    /*
     * Configure KMDF continuous reader on the Bulk IN pipe.
     * This replaces the original driver's multiple outstanding IN URBs
     * with reordering (Data_StartInTrans / Data_InTransReorder).
     * KMDF handles serialised delivery and error recovery.
     */
    {
        WDF_USB_CONTINUOUS_READER_CONFIG readerConfig;

        WDF_USB_CONTINUOUS_READER_CONFIG_INIT(
            &readerConfig,
            EvtUsbBulkInReadComplete,
            DevCtx,
            MAX_TRANSFER_SIZE
            );
        readerConfig.NumPendingReads          = NUM_CONTINUOUS_READERS;
        readerConfig.EvtUsbTargetPipeReadersFailed = EvtUsbBulkInReadersFailed;

        status = WdfUsbTargetPipeConfigContinuousReader(
            DevCtx->BulkInPipe,
            &readerConfig
            );
        if (!NT_SUCCESS(status)) {
            return status;
        }
    }

    /*
     * Configure continuous reader on the Interrupt IN pipe (if present)
     * for CDC ACM serial state notifications.
     */
    if (DevCtx->InterruptPipe != NULL) {
        WDF_USB_CONTINUOUS_READER_CONFIG readerConfig;

        WDF_USB_CONTINUOUS_READER_CONFIG_INIT(
            &readerConfig,
            EvtUsbInterruptReadComplete,
            DevCtx,
            INTERRUPT_BUFFER_SIZE
            );
        readerConfig.NumPendingReads          = 1;
        readerConfig.EvtUsbTargetPipeReadersFailed =
            EvtUsbInterruptReadersFailed;

        status = WdfUsbTargetPipeConfigContinuousReader(
            DevCtx->InterruptPipe,
            &readerConfig
            );
        if (!NT_SUCCESS(status)) {
            /* Non-fatal: device can function without serial state notifications */
            DevCtx->InterruptPipe = NULL;
        }
    }

    /* Create ZLP work item for zero-length packet after aligned writes */
    {
        WDF_WORKITEM_CONFIG workConfig;
        WDF_OBJECT_ATTRIBUTES workAttrs;

        WDF_WORKITEM_CONFIG_INIT(&workConfig, EvtZlpWorkItem);
        workConfig.AutomaticSerialization = FALSE;

        WDF_OBJECT_ATTRIBUTES_INIT(&workAttrs);
        workAttrs.ParentObject = DevCtx->Device;

        status = WdfWorkItemCreate(&workConfig, &workAttrs, &DevCtx->ZlpWorkItem);
        if (!NT_SUCCESS(status)) {
            return status;
        }
    }

    return STATUS_SUCCESS;
}

/* =========================================================================
 *  DeviceCreateSymbolicLink — create \\DosDevices\\COMx symbolic link
 *  Equivalent to the original PnP_CreateSymbolicLink (section 5)
 * ========================================================================= */
NTSTATUS
DeviceCreateSymbolicLink(
    _In_ PDEVICE_CONTEXT DevCtx
    )
{
    NTSTATUS    status;
    WDFKEY      hKey = NULL;

    PAGED_CODE();

    /* Initialise PortName unicode string */
    DevCtx->PortName.Buffer        = DevCtx->PortNameBuf;
    DevCtx->PortName.MaximumLength = sizeof(DevCtx->PortNameBuf);
    DevCtx->PortName.Length        = 0;

    /*
     * Stable port name lookup — same physical device, different PIDs.
     *
     * When the MTK device switches from Preloader (PID_2000) to DA (PID_2001)
     * it re-enumerates with a different hardware ID.  Windows would normally
     * assign a new COM port number.  To keep the same COMx, we:
     *
     *  1. Check HKLM\SOFTWARE\MediaTek\USBPortMap\<serialNumber> for a
     *     previously-stored port name (written the last time we assigned one).
     *  2. If found, use that port name directly (skip MsPorts assignment).
     *  3. If not found, proceed with the normal MsPorts.dll PortName, then
     *     store the assigned name so the next enumeration can reuse it.
     *
     * The lookup only happens when the device has a USB serial number string
     * (most MTK devices do).  Devices without one fall through to normal flow.
     */
    if (DevCtx->UsbSerialLen > 0) {
        UNICODE_STRING      mapKeyPath;
        OBJECT_ATTRIBUTES   objAttrs;
        HANDLE              hRawKey   = NULL;
        ULONG               disposition;
        WCHAR               mapKeyBuf[128];
        UNICODE_STRING      mapKeyName;

        /* Build the full registry key path */
        mapKeyPath.Buffer        = mapKeyBuf;
        mapKeyPath.MaximumLength = sizeof(mapKeyBuf);
        mapKeyPath.Length        = 0;

        status = RtlUnicodeStringPrintf(
            &mapKeyPath,
            L"%s",
            MTK_PORT_MAP_REGKEY
            );

        if (NT_SUCCESS(status)) {
            InitializeObjectAttributes(&objAttrs, &mapKeyPath,
                OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                NULL, NULL);

            status = ZwCreateKey(&hRawKey, KEY_ALL_ACCESS, &objAttrs,
                0, NULL, REG_OPTION_NON_VOLATILE, &disposition);

            if (NT_SUCCESS(status)) {
                WCHAR   portValBuf[32];
                ULONG   resultLen = 0;
                UCHAR   kviBuf[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(portValBuf)];
                PKEY_VALUE_PARTIAL_INFORMATION kvi =
                    (PKEY_VALUE_PARTIAL_INFORMATION)kviBuf;

                /* Value name = USB serial number string */
                mapKeyName.Buffer        = DevCtx->UsbSerialBuf;
                mapKeyName.MaximumLength = (USHORT)(sizeof(DevCtx->UsbSerialBuf));
                mapKeyName.Length        = DevCtx->UsbSerialLen;

                status = ZwQueryValueKey(
                    hRawKey,
                    &mapKeyName,
                    KeyValuePartialInformation,
                    kvi,
                    sizeof(kviBuf),
                    &resultLen
                    );

                if (NT_SUCCESS(status) &&
                    kvi->Type == REG_SZ &&
                    kvi->DataLength >= sizeof(WCHAR)) {
                    /* Found a previously-stored port name — use it */
                    USHORT copyLen = (USHORT)min(
                        kvi->DataLength,
                        (ULONG)(DevCtx->PortName.MaximumLength - sizeof(WCHAR))
                        );
                    RtlCopyMemory(DevCtx->PortName.Buffer, kvi->Data, copyLen);
                    DevCtx->PortName.Length = copyLen;
                    /* Null-terminate */
                    DevCtx->PortName.Buffer[copyLen / sizeof(WCHAR)] = L'\0';
                }
                /* Keep hRawKey open — we'll write back after assignment */
            }
        }

        /* Close the key; write-back is done in a separate block below */
        if (hRawKey != NULL) {
            ZwClose(hRawKey);
        }
    }

    /* Read PortName from device hardware registry key (set by MsPorts.dll) */
    if (DevCtx->PortName.Length == 0) {
        DECLARE_CONST_UNICODE_STRING(portNameValueName, L"PortName");

        status = WdfDeviceOpenRegistryKey(
            DevCtx->Device,
            PLUGPLAY_REGKEY_DEVICE,
            KEY_READ,
            WDF_NO_OBJECT_ATTRIBUTES,
            &hKey
            );
        if (NT_SUCCESS(status)) {
            status = WdfRegistryQueryUnicodeString(
                hKey,
                &portNameValueName,
                NULL,
                &DevCtx->PortName
                );
            WdfRegistryClose(hKey);
            hKey = NULL;
        }
    }

    /* Fall back to a generated name if PortName not found */
    if (DevCtx->PortName.Length == 0) {
        status = RtlUnicodeStringPrintf(
            &DevCtx->PortName,
            L"COM%d",
            (int)(DevCtx->PortIndex + 10)
            );
        if (!NT_SUCCESS(status)) {
            return status;
        }
    }

    /* Build DOS device name: \\DosDevices\\COMx */
    DevCtx->DosName.Buffer        = DevCtx->DosNameBuf;
    DevCtx->DosName.MaximumLength = sizeof(DevCtx->DosNameBuf);
    DevCtx->DosName.Length        = 0;

    status = RtlUnicodeStringPrintf(
        &DevCtx->DosName,
        L"\\DosDevices\\%wZ",
        &DevCtx->PortName
        );
    if (!NT_SUCCESS(status)) {
        return status;
    }

    /* Create symbolic link: \\DosDevices\\COMx -> \\Device\\mtkcdcacmN */
    status = IoCreateSymbolicLink(&DevCtx->DosName, &DevCtx->DeviceName);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    DevCtx->SymbolicLinkCreated = TRUE;

    /* Write SERIALCOMM registry entry so the system knows this COM port exists */
    status = RtlWriteRegistryValue(
        RTL_REGISTRY_DEVICEMAP,
        L"SERIALCOMM",
        DevCtx->DeviceName.Buffer,
        REG_SZ,
        DevCtx->PortName.Buffer,
        DevCtx->PortName.Length + sizeof(WCHAR)
        );

    if (NT_SUCCESS(status)) {
        DevCtx->SerialCommWritten = TRUE;
    }

    /*
     * Persist the assigned port name into the stable port map so that the
     * next enumeration of the same physical device (different PID) can reuse
     * the same COM port number.
     */
    if (DevCtx->UsbSerialLen > 0) {
        UNICODE_STRING      mapKeyPath;
        OBJECT_ATTRIBUTES   objAttrs;
        HANDLE              hRawKey = NULL;
        ULONG               disposition;
        WCHAR               mapKeyBuf[128];

        mapKeyPath.Buffer        = mapKeyBuf;
        mapKeyPath.MaximumLength = sizeof(mapKeyBuf);
        mapKeyPath.Length        = 0;

        if (NT_SUCCESS(RtlUnicodeStringPrintf(
                &mapKeyPath, L"%s", MTK_PORT_MAP_REGKEY))) {

            InitializeObjectAttributes(&objAttrs, &mapKeyPath,
                OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                NULL, NULL);

            if (NT_SUCCESS(ZwCreateKey(&hRawKey, KEY_ALL_ACCESS, &objAttrs,
                    0, NULL, REG_OPTION_NON_VOLATILE, &disposition))) {

                UNICODE_STRING mapKeyName;
                mapKeyName.Buffer        = DevCtx->UsbSerialBuf;
                mapKeyName.MaximumLength = (USHORT)(sizeof(DevCtx->UsbSerialBuf));
                mapKeyName.Length        = DevCtx->UsbSerialLen;

                ZwSetValueKey(
                    hRawKey,
                    &mapKeyName,
                    0,
                    REG_SZ,
                    DevCtx->PortName.Buffer,
                    DevCtx->PortName.Length + sizeof(WCHAR)
                    );

                ZwClose(hRawKey);
            }
        }
    }

    return STATUS_SUCCESS;
}

/* =========================================================================
 *  DeviceRemoveSymbolicLink — clean up COM port symbolic link
 *  Equivalent to the original PnP_RemoveSymbolicLinks
 * ========================================================================= */
VOID
DeviceRemoveSymbolicLink(
    _In_ PDEVICE_CONTEXT DevCtx
    )
{
    PAGED_CODE();

    if (DevCtx->SerialCommWritten) {
        RtlDeleteRegistryValue(
            RTL_REGISTRY_DEVICEMAP,
            L"SERIALCOMM",
            DevCtx->DeviceName.Buffer
            );
        DevCtx->SerialCommWritten = FALSE;
    }

    if (DevCtx->SymbolicLinkCreated) {
        IoDeleteSymbolicLink(&DevCtx->DosName);
        DevCtx->SymbolicLinkCreated = FALSE;
    }
}

/* =========================================================================
 *  DeviceReadRegistrySettings — read IdleTime, IdleEnable, etc.
 *  Equivalent to the original PWR_GetIdleInfo
 * ========================================================================= */
NTSTATUS
DeviceReadRegistrySettings(
    _In_ PDEVICE_CONTEXT DevCtx
    )
{
    NTSTATUS    status;
    WDFKEY      hKey = NULL;
    ULONG       value;

    PAGED_CODE();

    status = WdfDeviceOpenRegistryKey(
        DevCtx->Device,
        PLUGPLAY_REGKEY_DEVICE,
        KEY_READ,
        WDF_NO_OBJECT_ATTRIBUTES,
        &hKey
        );
    if (!NT_SUCCESS(status)) {
        return status;
    }

    {
        DECLARE_CONST_UNICODE_STRING(idleTimeName, L"IdleTime");
        if (NT_SUCCESS(WdfRegistryQueryULong(hKey, &idleTimeName, &value))) {
            DevCtx->IdleTimeSeconds = value;
        }
    }

    {
        DECLARE_CONST_UNICODE_STRING(idleEnableName, L"IdleEnable");
        if (NT_SUCCESS(WdfRegistryQueryULong(hKey, &idleEnableName, &value))) {
            DevCtx->IdleEnabled = (value != 0);
        }
    }

    {
        DECLARE_CONST_UNICODE_STRING(idleWWName, L"IdleWWBinded");
        if (NT_SUCCESS(WdfRegistryQueryULong(hKey, &idleWWName, &value))) {
            DevCtx->IdleWWBound = (value != 0);
        }
    }

    WdfRegistryClose(hKey);

    return STATUS_SUCCESS;
}
