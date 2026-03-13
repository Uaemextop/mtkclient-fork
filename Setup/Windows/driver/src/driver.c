/*
 * driver.c - KMDF Driver entry and device-add for mtkclient USB driver
 *
 * Copyright (c) 2024-2025 mtkclient contributors
 * Licensed under GPLv3
 *
 * This is the main entry point of the mtkclient KMDF USB function
 * driver.  It initialises the KMDF framework, registers the
 * EvtDeviceAdd callback, and performs driver-level cleanup.
 */

#include "driver.h"

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(PAGE, MtkEvtDeviceAdd)
#pragma alloc_text(PAGE, MtkEvtDriverContextCleanup)
#endif

/* ================================================================
 * DriverEntry – called by the OS when the driver is first loaded.
 *
 * Initialises the WDF driver object and registers the
 * EvtDeviceAdd callback so the framework calls us whenever PnP
 * discovers a matching device (VID/PID listed in the INF).
 * ================================================================ */
NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    )
{
    NTSTATUS            status;
    WDF_DRIVER_CONFIG   config;
    WDF_OBJECT_ATTRIBUTES attributes;

    /*
     * Register the EvtDeviceAdd callback.  The framework calls it
     * each time PnP enumerates one of our supported devices.
     */
    WDF_DRIVER_CONFIG_INIT(&config, MtkEvtDeviceAdd);

    /*
     * Register a cleanup callback for the driver object so we can
     * release any driver-wide resources on unload.
     */
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.EvtCleanupCallback = MtkEvtDriverContextCleanup;

    status = WdfDriverCreate(
        DriverObject,
        RegistryPath,
        &attributes,
        &config,
        WDF_NO_HANDLE
        );

    if (!NT_SUCCESS(status)) {
        KdPrint(("mtkclient: WdfDriverCreate failed 0x%x\n", status));
    }

    return status;
}

/* ================================================================
 * MtkEvtDeviceAdd – called when PnP discovers a matching device.
 *
 * Creates the WDFDEVICE, sets up PnP / power callbacks, creates
 * the device interface (GUID), and initialises the I/O queue.
 * ================================================================ */
NTSTATUS
MtkEvtDeviceAdd(
    _In_ WDFDRIVER       Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit
    )
{
    NTSTATUS                status;
    WDFDEVICE               device;
    WDF_OBJECT_ATTRIBUTES   deviceAttributes;
    WDF_PNPPOWER_EVENT_CALLBACKS pnpPowerCallbacks;
    PDEVICE_CONTEXT         devCtx;

    UNREFERENCED_PARAMETER(Driver);

    PAGED_CODE();

    KdPrint(("mtkclient: MtkEvtDeviceAdd\n"));

    /*
     * Register PnP / Power callbacks so we can initialise and tear
     * down USB resources when the device enters / leaves D0.
     */
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpPowerCallbacks);
    pnpPowerCallbacks.EvtDevicePrepareHardware = MtkEvtDevicePrepareHardware;
    pnpPowerCallbacks.EvtDeviceReleaseHardware = MtkEvtDeviceReleaseHardware;
    pnpPowerCallbacks.EvtDeviceD0Entry         = MtkEvtDeviceD0Entry;
    pnpPowerCallbacks.EvtDeviceD0Exit          = MtkEvtDeviceD0Exit;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpPowerCallbacks);

    /*
     * Allocate our per-device context alongside the WDFDEVICE.
     */
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&deviceAttributes, DEVICE_CONTEXT);

    status = WdfDeviceCreate(&DeviceInit, &deviceAttributes, &device);
    if (!NT_SUCCESS(status)) {
        KdPrint(("mtkclient: WdfDeviceCreate failed 0x%x\n", status));
        return status;
    }

    /*
     * Initialise the device context with safe defaults.
     */
    devCtx = DeviceGetContext(device);
    RtlZeroMemory(devCtx, sizeof(DEVICE_CONTEXT));
    devCtx->Connected = FALSE;

    /* Default CDC line coding: 115200 8-N-1 */
    devCtx->LineCoding.BaudRate = 115200;
    devCtx->LineCoding.StopBits = 0;    /* 1 stop bit */
    devCtx->LineCoding.Parity   = 0;    /* None       */
    devCtx->LineCoding.DataBits = 8;

    /*
     * Create a device interface so user-mode applications can
     * discover this device via SetupDi / CM APIs.
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

    /*
     * Set up the I/O queues (read, write, IOCTL).
     */
    status = MtkQueueInitialize(device);
    if (!NT_SUCCESS(status)) {
        KdPrint(("mtkclient: MtkQueueInitialize failed 0x%x\n", status));
        return status;
    }

    KdPrint(("mtkclient: Device added successfully\n"));
    return status;
}

/* ================================================================
 * MtkEvtDriverContextCleanup – driver object cleanup.
 * ================================================================ */
VOID
MtkEvtDriverContextCleanup(
    _In_ WDFOBJECT DriverObject
    )
{
    UNREFERENCED_PARAMETER(DriverObject);
    PAGED_CODE();

    KdPrint(("mtkclient: Driver cleanup\n"));
}
