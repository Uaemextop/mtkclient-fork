/*
 * device.c - mtkclient KMDF WinUSB Function Driver - Device Callbacks
 * (c) 2024-2026 GPLv3 License
 *
 * Implements PnP and power management callbacks:
 *   - PrepareHardware: Initialize USB device target, select configuration,
 *     enumerate pipes, configure selective suspend
 *   - ReleaseHardware: Clean up USB target on device removal
 *   - D0Entry/D0Exit: Handle power state transitions
 *
 * The driver does not perform USB I/O in kernel mode. All data transfer
 * is handled by user-mode code via WinUSB API through the device interface.
 */

#include "driver.h"

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, MtkEvtDevicePrepareHardware)
#pragma alloc_text(PAGE, MtkEvtDeviceReleaseHardware)
#endif

/*
 * MtkEvtDevicePrepareHardware - Initialize USB device
 *
 * Called by KMDF when the device enters D0 state. Creates the USB device
 * target, selects the first configuration, and enumerates bulk endpoints.
 */
NTSTATUS
MtkEvtDevicePrepareHardware(
    _In_ WDFDEVICE    Device,
    _In_ WDFCMRESLIST ResourcesRaw,
    _In_ WDFCMRESLIST ResourcesTranslated
    )
{
    NTSTATUS status;
    PDEVICE_CONTEXT deviceContext;
    WDF_USB_DEVICE_CREATE_CONFIG createParams;
    WDF_USB_DEVICE_SELECT_CONFIG_PARAMS configParams;
    WDFUSBINTERFACE usbInterface;
    UCHAR numEndpoints;
    UCHAR i;

    UNREFERENCED_PARAMETER(ResourcesRaw);
    UNREFERENCED_PARAMETER(ResourcesTranslated);

    PAGED_CODE();

    KdPrint(("mtkclient: PrepareHardware\n"));

    deviceContext = DeviceGetContext(Device);

    /* Create USB device target with KMDF 1.11+ client contract */
    WDF_USB_DEVICE_CREATE_CONFIG_INIT(&createParams,
                                       USBD_CLIENT_CONTRACT_VERSION_602);

    status = WdfUsbTargetDeviceCreateWithParameters(
        Device,
        &createParams,
        WDF_NO_OBJECT_ATTRIBUTES,
        &deviceContext->UsbDevice
    );

    if (!NT_SUCCESS(status)) {
        KdPrint(("mtkclient: WdfUsbTargetDeviceCreateWithParameters failed 0x%x\n", status));
        return status;
    }

    /* Select the first USB configuration (single configuration device) */
    WDF_USB_DEVICE_SELECT_CONFIG_PARAMS_INIT_SINGLE_INTERFACE(&configParams);

    status = WdfUsbTargetDeviceSelectConfig(
        deviceContext->UsbDevice,
        WDF_NO_OBJECT_ATTRIBUTES,
        &configParams
    );

    if (!NT_SUCCESS(status)) {
        KdPrint(("mtkclient: WdfUsbTargetDeviceSelectConfig failed 0x%x\n", status));
        return status;
    }

    /* Get the USB interface object */
    deviceContext->UsbInterface = configParams.Types.SingleInterface.ConfiguredUsbInterface;
    usbInterface = deviceContext->UsbInterface;

    /* Enumerate endpoints to find bulk IN and OUT pipes */
    numEndpoints = WdfUsbInterfaceGetNumEndpoints(usbInterface, 0);
    KdPrint(("mtkclient: Found %d endpoints\n", numEndpoints));

    for (i = 0; i < numEndpoints; i++) {
        WDF_USB_PIPE_INFORMATION pipeInfo;

        WDF_USB_PIPE_INFORMATION_INIT(&pipeInfo);
        WdfUsbInterfaceGetEndpointInformation(usbInterface, 0, i, &pipeInfo);

        if (pipeInfo.PipeType == WdfUsbPipeTypeBulk) {
            if (USB_ENDPOINT_DIRECTION_IN(pipeInfo.EndpointAddress)) {
                deviceContext->BulkInPipe = pipeInfo.EndpointAddress;
                KdPrint(("mtkclient: Bulk IN endpoint 0x%02x (max %d bytes)\n",
                         pipeInfo.EndpointAddress, pipeInfo.MaximumPacketSize));
            } else {
                deviceContext->BulkOutPipe = pipeInfo.EndpointAddress;
                KdPrint(("mtkclient: Bulk OUT endpoint 0x%02x (max %d bytes)\n",
                         pipeInfo.EndpointAddress, pipeInfo.MaximumPacketSize));
            }
        }
    }

    /* Store VID/PID from device descriptor */
    {
        USB_DEVICE_DESCRIPTOR deviceDescriptor;
        WdfUsbTargetDeviceGetDeviceDescriptor(deviceContext->UsbDevice,
                                               &deviceDescriptor);
        deviceContext->VendorId = deviceDescriptor.idVendor;
        deviceContext->ProductId = deviceDescriptor.idProduct;
        KdPrint(("mtkclient: Device VID=%04x PID=%04x\n",
                 deviceDescriptor.idVendor, deviceDescriptor.idProduct));
    }

    /*
     * Configure USB selective suspend policy.
     * Disable auto-suspend to prevent Windows from suspending the device
     * during active mtkclient operations (bootrom handshake, DA transfer).
     */
    {
        WDF_DEVICE_POWER_POLICY_IDLE_SETTINGS idleSettings;

        WDF_DEVICE_POWER_POLICY_IDLE_SETTINGS_INIT(&idleSettings,
                                                     IdleCannotWakeFromS0);
        idleSettings.IdleTimeout = 10000; /* 10 seconds */
        idleSettings.UserControlOfIdleSettings = IdleAllowUserControl;
        idleSettings.Enabled = WdfFalse; /* Disabled by default for MTK devices */

        status = WdfDeviceAssignS0IdleSettings(Device, &idleSettings);
        if (!NT_SUCCESS(status)) {
            /* Non-fatal: selective suspend just won't be managed */
            KdPrint(("mtkclient: S0IdleSettings failed 0x%x (non-fatal)\n", status));
            status = STATUS_SUCCESS;
        }
    }

    KdPrint(("mtkclient: PrepareHardware complete\n"));
    return status;
}

/*
 * MtkEvtDeviceReleaseHardware - Cleanup on device removal
 *
 * Called when device is being removed or system is shutting down.
 * KMDF automatically cleans up the USB target and interface objects.
 */
NTSTATUS
MtkEvtDeviceReleaseHardware(
    _In_ WDFDEVICE    Device,
    _In_ WDFCMRESLIST ResourcesTranslated
    )
{
    PDEVICE_CONTEXT deviceContext;

    UNREFERENCED_PARAMETER(ResourcesTranslated);

    PAGED_CODE();

    KdPrint(("mtkclient: ReleaseHardware\n"));

    deviceContext = DeviceGetContext(Device);

    /* Zero out context - KMDF handles cleanup of USB objects */
    if (deviceContext) {
        deviceContext->UsbDevice = NULL;
        deviceContext->UsbInterface = NULL;
        deviceContext->BulkInPipe = 0;
        deviceContext->BulkOutPipe = 0;
    }

    return STATUS_SUCCESS;
}

/*
 * MtkEvtDeviceD0Entry - Device entering D0 (powered) state
 *
 * Called when device powers up. MediaTek devices re-enumerate
 * frequently (bootrom → preloader → DA → normal), so this is
 * called multiple times during a typical mtkclient session.
 */
NTSTATUS
MtkEvtDeviceD0Entry(
    _In_ WDFDEVICE              Device,
    _In_ WDF_POWER_DEVICE_STATE PreviousState
    )
{
    UNREFERENCED_PARAMETER(Device);

    KdPrint(("mtkclient: D0Entry from state %d\n", PreviousState));
    return STATUS_SUCCESS;
}

/*
 * MtkEvtDeviceD0Exit - Device leaving D0 state
 *
 * Called when device is being powered down or removed.
 */
NTSTATUS
MtkEvtDeviceD0Exit(
    _In_ WDFDEVICE              Device,
    _In_ WDF_POWER_DEVICE_STATE TargetState
    )
{
    UNREFERENCED_PARAMETER(Device);

    KdPrint(("mtkclient: D0Exit to state %d\n", TargetState));
    return STATUS_SUCCESS;
}
