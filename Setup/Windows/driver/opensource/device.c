/*
 * device.c
 *
 * PnP/Power lifecycle, USB configuration, pipe setup, symbolic link
 * management, and file object (Create/Close/Cleanup) handlers.
 *
 * SPDX-License-Identifier: MIT
 */

#include "mtk_usb2ser.h"

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, EvtDevicePrepareHardware)
#pragma alloc_text(PAGE, EvtDeviceReleaseHardware)
#pragma alloc_text(PAGE, EvtDeviceD0Entry)
#pragma alloc_text(PAGE, EvtDeviceD0Exit)
#pragma alloc_text(PAGE, EvtDeviceFileCreate)
#pragma alloc_text(PAGE, EvtFileClose)
#pragma alloc_text(PAGE, EvtFileCleanup)
#pragma alloc_text(PAGE, DeviceConfigureUsbDevice)
#pragma alloc_text(PAGE, DeviceConfigureUsbPipes)
#pragma alloc_text(PAGE, DeviceCreateSymbolicLink)
#pragma alloc_text(PAGE, DeviceRemoveSymbolicLink)
#pragma alloc_text(PAGE, DeviceReadRegistrySettings)
#endif

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

    status = DeviceConfigureUsbDevice(devCtx);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    status = DeviceConfigureUsbPipes(devCtx);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    status = RingBufferInit(&devCtx->ReadBuffer, DEFAULT_READ_BUFFER_SIZE);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    DeviceReadRegistrySettings(devCtx);

    status = DeviceCreateSymbolicLink(devCtx);
    if (!NT_SUCCESS(status)) {
        /* Non-fatal */
    }

    PowerConfigureIdleSettings(devCtx);

    devCtx->DeviceStarted = TRUE;

    return STATUS_SUCCESS;
}

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

    DeviceRemoveSymbolicLink(devCtx);
    RingBufferFree(&devCtx->ReadBuffer);

    return STATUS_SUCCESS;
}

NTSTATUS
EvtDeviceD0Entry(
    _In_ WDFDEVICE              Device,
    _In_ WDF_POWER_DEVICE_STATE PreviousState
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(PreviousState);
    return STATUS_SUCCESS;
}

NTSTATUS
EvtDeviceD0Exit(
    _In_ WDFDEVICE              Device,
    _In_ WDF_POWER_DEVICE_STATE TargetState
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(TargetState);
    return STATUS_SUCCESS;
}

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

    RingBufferPurge(&devCtx->ReadBuffer);
    RtlZeroMemory(&devCtx->PerfStats, sizeof(SERIALPERF_STATS));

    UsbControlSetDtr(devCtx);
    UsbControlSetRts(devCtx);

    status = UsbControlSetLineCoding(devCtx);
    UNREFERENCED_PARAMETER(status);

    devCtx->DeviceOpen = TRUE;

    WdfRequestComplete(Request, STATUS_SUCCESS);
}

VOID
EvtFileClose(
    _In_ WDFFILEOBJECT FileObject
    )
{
    WDFDEVICE       device = WdfFileObjectGetDevice(FileObject);
    PDEVICE_CONTEXT devCtx = GetDeviceContext(device);

    PAGED_CODE();

    devCtx->DeviceOpen = FALSE;

    UsbControlClrDtr(devCtx);
    UsbControlClrRts(devCtx);
}

VOID
EvtFileCleanup(
    _In_ WDFFILEOBJECT FileObject
    )
{
    WDFDEVICE       device = WdfFileObjectGetDevice(FileObject);
    PDEVICE_CONTEXT devCtx = GetDeviceContext(device);

    PAGED_CODE();

    WdfIoQueuePurgeSynchronously(devCtx->PendingReadQueue);
    WdfIoQueuePurgeSynchronously(devCtx->PendingWaitMaskQueue);

    WdfIoQueueStart(devCtx->PendingReadQueue);
    WdfIoQueueStart(devCtx->PendingWaitMaskQueue);

    RingBufferPurge(&devCtx->ReadBuffer);

    WdfSpinLockAcquire(devCtx->EventLock);
    devCtx->WaitMask    = 0;
    devCtx->EventHistory = 0;
    WdfSpinLockRelease(devCtx->EventLock);
}

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

    return status;
}

NTSTATUS
DeviceConfigureUsbPipes(
    _In_ PDEVICE_CONTEXT DevCtx
    )
{
    NTSTATUS                    status = STATUS_SUCCESS;
    BYTE                        numInterfaces;
    BYTE                        interfaceIdx, pipeIdx;
    WDF_USB_PIPE_INFORMATION    pipeInfo;

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
                        DevCtx->InterruptPipe    = pipe;
                        DevCtx->InterfaceNumber  =
                            WdfUsbInterfaceGetInterfaceNumber(usbInterface);
                    }
                }
            }
        }
    }

    if (DevCtx->BulkInPipe == NULL || DevCtx->BulkOutPipe == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    if (DevCtx->InterruptPipe == NULL && DevCtx->UsbInterface != NULL) {
        DevCtx->InterfaceNumber =
            WdfUsbInterfaceGetInterfaceNumber(DevCtx->UsbInterface);
    }

    WdfUsbTargetPipeSetNoMaximumPacketSizeCheck(DevCtx->BulkInPipe);
    WdfUsbTargetPipeSetNoMaximumPacketSizeCheck(DevCtx->BulkOutPipe);

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
            DevCtx->InterruptPipe = NULL;
        }
    }

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

NTSTATUS
DeviceCreateSymbolicLink(
    _In_ PDEVICE_CONTEXT DevCtx
    )
{
    NTSTATUS    status;
    WDFKEY      hKey = NULL;

    PAGED_CODE();

    DevCtx->PortName.Buffer        = DevCtx->PortNameBuf;
    DevCtx->PortName.MaximumLength = sizeof(DevCtx->PortNameBuf);
    DevCtx->PortName.Length        = 0;

    {
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

    if (!NT_SUCCESS(status) || DevCtx->PortName.Length == 0) {
        status = RtlUnicodeStringPrintf(
            &DevCtx->PortName,
            L"COM%d",
            (int)(DevCtx->PortIndex + 10)
            );
        if (!NT_SUCCESS(status)) {
            return status;
        }
    }

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

    status = IoCreateSymbolicLink(&DevCtx->DosName, &DevCtx->DeviceName);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    DevCtx->SymbolicLinkCreated = TRUE;

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

    return STATUS_SUCCESS;
}

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
