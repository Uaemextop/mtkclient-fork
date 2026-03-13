/*
 * driver.c - mtkclient KMDF WinUSB Function Driver - Entry Point
 * (c) 2024-2026 GPLv3 License
 *
 * Minimal KMDF driver entry point. Creates a device object and registers
 * callbacks for hardware prepare/release and power management.
 *
 * This driver delegates all USB I/O to WinUSB.sys via the KMDF USB
 * target framework. The user-mode application (mtkclient) communicates
 * with WinUSB.sys directly through the WinUSB API.
 */

#include "driver.h"

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(PAGE, MtkEvtDeviceAdd)
#endif

/*
 * DriverEntry - KMDF driver initialization
 *
 * Creates the WDFDRIVER object and registers the EvtDeviceAdd callback.
 * This is the kernel-mode equivalent of main() for WDM/KMDF drivers.
 */
NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    )
{
    WDF_DRIVER_CONFIG config;
    NTSTATUS status;

    /* Initialize driver config with device-add callback */
    WDF_DRIVER_CONFIG_INIT(&config, MtkEvtDeviceAdd);

    /* Create the KMDF driver object */
    status = WdfDriverCreate(
        DriverObject,
        RegistryPath,
        WDF_NO_OBJECT_ATTRIBUTES,
        &config,
        WDF_NO_HANDLE
    );

    if (!NT_SUCCESS(status)) {
        KdPrint(("mtkclient: WdfDriverCreate failed 0x%x\n", status));
    }

    return status;
}

/*
 * MtkEvtDeviceAdd - Called when PnP manager finds a matching device
 *
 * Creates the KMDF device object, sets up PnP/power callbacks,
 * and registers the device interface GUID for user-mode discovery.
 */
NTSTATUS
MtkEvtDeviceAdd(
    _In_ WDFDRIVER       Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit
    )
{
    NTSTATUS status;
    WDFDEVICE device;
    PDEVICE_CONTEXT deviceContext;
    WDF_OBJECT_ATTRIBUTES deviceAttributes;
    WDF_PNPPOWER_EVENT_CALLBACKS pnpPowerCallbacks;

    UNREFERENCED_PARAMETER(Driver);

    PAGED_CODE();

    KdPrint(("mtkclient: MtkEvtDeviceAdd\n"));

    /*
     * Register PnP and power management callbacks.
     * PrepareHardware initializes the USB device.
     * ReleaseHardware cleans up on removal.
     * D0Entry/D0Exit handle power state transitions.
     */
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpPowerCallbacks);
    pnpPowerCallbacks.EvtDevicePrepareHardware = MtkEvtDevicePrepareHardware;
    pnpPowerCallbacks.EvtDeviceReleaseHardware = MtkEvtDeviceReleaseHardware;
    pnpPowerCallbacks.EvtDeviceD0Entry = MtkEvtDeviceD0Entry;
    pnpPowerCallbacks.EvtDeviceD0Exit = MtkEvtDeviceD0Exit;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpPowerCallbacks);

    /* Set device context type */
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&deviceAttributes, DEVICE_CONTEXT);

    /* Create the KMDF device object */
    status = WdfDeviceCreate(&DeviceInit, &deviceAttributes, &device);
    if (!NT_SUCCESS(status)) {
        KdPrint(("mtkclient: WdfDeviceCreate failed 0x%x\n", status));
        return status;
    }

    /* Initialize device context */
    deviceContext = DeviceGetContext(device);
    RtlZeroMemory(deviceContext, sizeof(DEVICE_CONTEXT));

    /*
     * Create the device interface using the mtkclient GUID.
     * User-mode applications (mtk_usb_driver.dll, win32_utils.py)
     * use this GUID via SetupDiGetClassDevs to find the device.
     */
    status = WdfDeviceCreateDeviceInterface(
        device,
        &GUID_DEVINTERFACE_MTKCLIENT,
        NULL
    );

    if (!NT_SUCCESS(status)) {
        KdPrint(("mtkclient: WdfDeviceCreateDeviceInterface failed 0x%x\n", status));
        return status;
    }

    KdPrint(("mtkclient: Device interface created successfully\n"));

    return status;
}
