/*
 * driver.c — MediaTek CDC/ACM Serial KMDF Driver — Entry & DeviceAdd
 *
 * Clean-room KMDF reimplementation of CDC Abstract Control Model
 * serial-port functionality for MediaTek BROM / Preloader / DA devices.
 * Replaces the proprietary usb2ser.sys shipped with MTK SP Drivers.
 *
 * This file contains:
 *   - DriverEntry            — KMDF driver initialisation
 *   - MtkSerialEvtDeviceAdd  — PnP device creation, queues, interfaces
 *   - File-object callbacks  — Create / Close for serial-port semantics
 *
 * Build requirements:
 *   Visual Studio 2022 + Windows Driver Kit (WDK) 10
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2024 mtkclient contributors
 */

#include <initguid.h>
#include "mtk_serial.h"

/* Global COM-port instance counter (first port = COM100) */
static LONG g_InstanceCounter = 99;

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(PAGE, MtkSerialEvtDeviceAdd)
#pragma alloc_text(PAGE, MtkSerialEvtDeviceFileCreate)
#pragma alloc_text(PAGE, MtkSerialEvtFileClose)
#endif

/* ----------------------------------------------------------------
 * DriverEntry — KMDF driver initialisation
 * ---------------------------------------------------------------- */
NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    )
{
    WDF_DRIVER_CONFIG config;
    NTSTATUS          status;

    WDF_DRIVER_CONFIG_INIT(&config, MtkSerialEvtDeviceAdd);

    status = WdfDriverCreate(
        DriverObject,
        RegistryPath,
        WDF_NO_OBJECT_ATTRIBUTES,
        &config,
        WDF_NO_HANDLE);

    return status;
}

/* ----------------------------------------------------------------
 * MtkSerialEvtDeviceAdd — Create device, interfaces and queues
 * ---------------------------------------------------------------- */
NTSTATUS
MtkSerialEvtDeviceAdd(
    _In_    WDFDRIVER       Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit
    )
{
    WDF_PNPPOWER_EVENT_CALLBACKS    pnpPower;
    WDF_FILEOBJECT_CONFIG           fileConfig;
    WDF_OBJECT_ATTRIBUTES           devAttrs;
    WDFDEVICE                       device;
    PSERIAL_DEVICE_CONTEXT          pCtx;
    NTSTATUS                        status;
    LONG                            portNum;
    DECLARE_UNICODE_STRING_SIZE(deviceName, 64);
    DECLARE_UNICODE_STRING_SIZE(dosDeviceName, 64);

    PAGED_CODE();
    UNREFERENCED_PARAMETER(Driver);

    /* ---- Assign a stable device name (\Device\cdcacmN) ---- */
    portNum = InterlockedIncrement(&g_InstanceCounter);

    status = RtlUnicodeStringPrintf(
        &deviceName, L"%ws%d", MTK_DEVICE_NAME_PREFIX, portNum - 100);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    status = WdfDeviceInitAssignName(DeviceInit, &deviceName);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    /* ---- PnP / Power callbacks ---- */
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpPower);
    pnpPower.EvtDevicePrepareHardware  = MtkSerialEvtDevicePrepareHardware;
    pnpPower.EvtDeviceReleaseHardware  = MtkSerialEvtDeviceReleaseHardware;
    pnpPower.EvtDeviceD0Entry          = MtkSerialEvtDeviceD0Entry;
    pnpPower.EvtDeviceD0Exit           = MtkSerialEvtDeviceD0Exit;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpPower);

    /* ---- File-object callbacks (serial Create / Close) ---- */
    WDF_FILEOBJECT_CONFIG_INIT(
        &fileConfig,
        MtkSerialEvtDeviceFileCreate,
        MtkSerialEvtFileClose,
        WDF_NO_EVENT_CALLBACK);     /* no Cleanup needed */
    WdfDeviceInitSetFileObjectConfig(DeviceInit, &fileConfig,
                                     WDF_NO_OBJECT_ATTRIBUTES);

    /* Allow direct I/O for best bulk-transfer throughput */
    WdfDeviceInitSetIoType(DeviceInit, WdfDeviceIoDirect);

    /* ---- Create the device object ---- */
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&devAttrs, SERIAL_DEVICE_CONTEXT);

    status = WdfDeviceCreate(&DeviceInit, &devAttrs, &device);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    pCtx = SerialGetDeviceContext(device);
    RtlZeroMemory(pCtx, sizeof(*pCtx));

    /* Store port number and formatted names in context */
    pCtx->PortNumber = portNum;

    RtlInitEmptyUnicodeString(
        &pCtx->DeviceName, pCtx->DeviceNameBuf,
        sizeof(pCtx->DeviceNameBuf));
    RtlCopyUnicodeString(&pCtx->DeviceName, &deviceName);

    status = RtlUnicodeStringPrintf(
        &dosDeviceName, L"%ws%d", MTK_DOSDEVICE_PREFIX, portNum);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    RtlInitEmptyUnicodeString(
        &pCtx->DosDeviceName, pCtx->DosDeviceNameBuf,
        sizeof(pCtx->DosDeviceNameBuf));
    RtlCopyUnicodeString(&pCtx->DosDeviceName, &dosDeviceName);

    status = RtlStringCbPrintfW(
        pCtx->ComPortName, sizeof(pCtx->ComPortName),
        L"COM%d", portNum);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    /* ---- Create the DOS-device symbolic link ---- */
    status = WdfDeviceCreateSymbolicLink(device, &dosDeviceName);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    pCtx->SymLinkCreated = TRUE;

    /* ---- Create wait-lock for serial state ---- */
    status = WdfWaitLockCreate(WDF_NO_OBJECT_ATTRIBUTES, &pCtx->SerialLock);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    /* ---- Register device interfaces ---- */
    status = WdfDeviceCreateDeviceInterface(
        device, &GUID_DEVINTERFACE_MTK_SERIAL, NULL);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    /* ---- Create I/O queues ---- */
    status = MtkSerialCreateQueues(device);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    /* ---- Set default serial parameters ---- */
    pCtx->LineCoding.dwDTERate  = MTK_DEFAULT_BAUD_RATE;
    pCtx->LineCoding.bCharFormat = MTK_DEFAULT_STOP_BITS;
    pCtx->LineCoding.bParityType = MTK_DEFAULT_PARITY;
    pCtx->LineCoding.bDataBits  = MTK_DEFAULT_DATA_BITS;

    return STATUS_SUCCESS;
}

/* ----------------------------------------------------------------
 * MtkSerialEvtDeviceFileCreate — Handle serial-port open
 * ---------------------------------------------------------------- */
VOID
MtkSerialEvtDeviceFileCreate(
    _In_ WDFDEVICE     Device,
    _In_ WDFREQUEST    Request,
    _In_ WDFFILEOBJECT FileObject
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(FileObject);

    WdfRequestComplete(Request, STATUS_SUCCESS);
}

/* ----------------------------------------------------------------
 * MtkSerialEvtFileClose — Handle serial-port close
 * ---------------------------------------------------------------- */
VOID
MtkSerialEvtFileClose(
    _In_ WDFFILEOBJECT FileObject
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(FileObject);
}
