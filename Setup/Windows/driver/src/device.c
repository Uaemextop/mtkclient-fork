/*
 * device.c — MediaTek CDC/ACM Serial KMDF Driver — USB Device Setup
 *
 * USB device configuration, interface selection, pipe setup,
 * power management (D0 Entry / Exit) and COM-port registration.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2024 mtkclient contributors
 */

#include "mtk_serial.h"

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, MtkSerialEvtDevicePrepareHardware)
#pragma alloc_text(PAGE, MtkSerialEvtDeviceReleaseHardware)
#pragma alloc_text(PAGE, MtkSerialEvtDeviceD0Entry)
#pragma alloc_text(PAGE, MtkSerialEvtDeviceD0Exit)
#pragma alloc_text(PAGE, MtkSerialSelectInterfaces)
#pragma alloc_text(PAGE, MtkSerialConfigurePipes)
#pragma alloc_text(PAGE, MtkSerialRegisterComPort)
#pragma alloc_text(PAGE, MtkSerialUnregisterComPort)
#endif

/* ----------------------------------------------------------------
 * EvtDevicePrepareHardware — Configure the USB device
 *
 * Creates the WDF USB target, selects the configuration, finds
 * the CDC control + data interfaces, configures pipe policies,
 * sends the initial line coding, and registers the COM port.
 * ---------------------------------------------------------------- */
NTSTATUS
MtkSerialEvtDevicePrepareHardware(
    _In_ WDFDEVICE    Device,
    _In_ WDFCMRESLIST ResourceList,
    _In_ WDFCMRESLIST ResourceListTranslated
    )
{
    PSERIAL_DEVICE_CONTEXT          pCtx;
    WDF_USB_DEVICE_CREATE_CONFIG    usbCreateCfg;
    NTSTATUS                        status;

    PAGED_CODE();
    UNREFERENCED_PARAMETER(ResourceList);
    UNREFERENCED_PARAMETER(ResourceListTranslated);

    pCtx = SerialGetDeviceContext(Device);

    /* ---- Create USB device object ---- */
    WDF_USB_DEVICE_CREATE_CONFIG_INIT(
        &usbCreateCfg,
        USBD_CLIENT_CONTRACT_VERSION_602);

    status = WdfUsbTargetDeviceCreateWithParameters(
        Device, &usbCreateCfg,
        WDF_NO_OBJECT_ATTRIBUTES,
        &pCtx->UsbDevice);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    /* ---- Select interfaces (CDC control + data, or single) ---- */
    status = MtkSerialSelectInterfaces(Device);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    /* ---- Configure bulk / interrupt pipes ---- */
    status = MtkSerialConfigurePipes(Device);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    /* ---- Push default line coding to hardware ---- */
    status = MtkSerialSetLineCoding(Device, &pCtx->LineCoding);
    if (!NT_SUCCESS(status)) {
        /* Non-fatal — some bootloaders ignore SET_LINE_CODING */
    }

    /* ---- Register COM port in SERIALCOMM ---- */
    status = MtkSerialRegisterComPort(Device);
    if (!NT_SUCCESS(status)) {
        /* Non-fatal — COM port name just won't appear */
    }

    return STATUS_SUCCESS;
}

/* ----------------------------------------------------------------
 * EvtDeviceReleaseHardware — Tear-down on removal / surprise-remove
 * ---------------------------------------------------------------- */
NTSTATUS
MtkSerialEvtDeviceReleaseHardware(
    _In_ WDFDEVICE    Device,
    _In_ WDFCMRESLIST ResourceListTranslated
    )
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(ResourceListTranslated);

    MtkSerialUnregisterComPort(Device);

    return STATUS_SUCCESS;
}

/* ----------------------------------------------------------------
 * EvtDeviceD0Entry — Activate DTR when entering powered-on state
 * ---------------------------------------------------------------- */
NTSTATUS
MtkSerialEvtDeviceD0Entry(
    _In_ WDFDEVICE              Device,
    _In_ WDF_POWER_DEVICE_STATE PreviousState
    )
{
    PSERIAL_DEVICE_CONTEXT pCtx;
    USHORT signals;

    PAGED_CODE();
    UNREFERENCED_PARAMETER(PreviousState);

    pCtx = SerialGetDeviceContext(Device);

    /* Activate DTR so the device knows the host is ready */
    pCtx->DtrEnabled = TRUE;
    signals = CDC_CONTROL_LINE_DTR;
    if (pCtx->RtsEnabled) {
        signals |= CDC_CONTROL_LINE_RTS;
    }
    pCtx->ControlLineState = signals;

    /* Best-effort — bootloaders may not implement this request */
    (VOID) MtkSerialSetControlLineState(Device, signals);

    /* Report CTS + DSR in modem status (virtual, always asserted) */
    pCtx->ModemStatus = SERIAL_MSR_CTS | SERIAL_MSR_DSR;

    return STATUS_SUCCESS;
}

/* ----------------------------------------------------------------
 * EvtDeviceD0Exit — Deactivate DTR/RTS, cancel pending I/O
 * ---------------------------------------------------------------- */
NTSTATUS
MtkSerialEvtDeviceD0Exit(
    _In_ WDFDEVICE              Device,
    _In_ WDF_POWER_DEVICE_STATE TargetState
    )
{
    PSERIAL_DEVICE_CONTEXT pCtx;

    PAGED_CODE();
    UNREFERENCED_PARAMETER(TargetState);

    pCtx = SerialGetDeviceContext(Device);

    /* Deactivate control lines */
    pCtx->DtrEnabled = FALSE;
    pCtx->RtsEnabled = FALSE;
    pCtx->ControlLineState = 0;
    (VOID) MtkSerialSetControlLineState(Device, 0);

    /* Abort outstanding transfers so the device can power down */
    if (pCtx->BulkReadPipe != NULL) {
        WdfUsbTargetPipeAbortSynchronously(
            pCtx->BulkReadPipe, WDF_NO_HANDLE, NULL);
    }
    if (pCtx->BulkWritePipe != NULL) {
        WdfUsbTargetPipeAbortSynchronously(
            pCtx->BulkWritePipe, WDF_NO_HANDLE, NULL);
    }

    return STATUS_SUCCESS;
}

/* ----------------------------------------------------------------
 * MtkSerialSelectInterfaces
 *
 * Parse the USB descriptors to find the CDC control interface
 * (class 0x02 / subclass 0x02) and the CDC data interface
 * (class 0x0A).  Falls back to single-interface selection when
 * the device is not a standard composite CDC device.
 * ---------------------------------------------------------------- */
NTSTATUS
MtkSerialSelectInterfaces(
    _In_ WDFDEVICE Device
    )
{
    PSERIAL_DEVICE_CONTEXT                  pCtx;
    WDF_USB_DEVICE_SELECT_CONFIG_PARAMS     cfgParams;
    USB_DEVICE_DESCRIPTOR                   devDesc;
    NTSTATUS                                status;
    UCHAR                                   numInterfaces;
    UCHAR                                   i;

    PAGED_CODE();

    pCtx = SerialGetDeviceContext(Device);

    WdfUsbTargetDeviceGetDeviceDescriptor(pCtx->UsbDevice, &devDesc);
    numInterfaces = WdfUsbTargetDeviceGetNumInterfaces(pCtx->UsbDevice);

    if (numInterfaces >= 2) {
        /*
         * Multi-interface CDC/ACM: typically interface 0 = CDC control
         * (class 0x02 / sub 0x02) and interface 1 = CDC data (class 0x0A).
         * Use INIT_MULTIPLE_INTERFACES with auto-dispatch.
         */
        WDF_USB_DEVICE_SELECT_CONFIG_PARAMS_INIT_MULTIPLE_INTERFACES(
            &cfgParams, 0, NULL);

        status = WdfUsbTargetDeviceSelectConfig(
            pCtx->UsbDevice,
            WDF_NO_OBJECT_ATTRIBUTES,
            &cfgParams);
        if (!NT_SUCCESS(status)) {
            return status;
        }

        /* Walk interfaces and identify control vs. data */
        for (i = 0; i < numInterfaces; i++) {
            WDFUSBINTERFACE                 iface;
            USB_INTERFACE_DESCRIPTOR        ifaceDesc;

            iface = WdfUsbTargetDeviceGetInterface(pCtx->UsbDevice, i);
            if (iface == NULL) {
                continue;
            }

            WdfUsbInterfaceGetDescriptor(iface, 0, &ifaceDesc);

            if (ifaceDesc.bInterfaceClass == CDC_COMM_INTERFACE_CLASS &&
                ifaceDesc.bInterfaceSubClass == CDC_ACM_INTERFACE_SUBCLASS) {
                pCtx->UsbControlInterface = iface;
            } else if (ifaceDesc.bInterfaceClass == CDC_DATA_INTERFACE_CLASS) {
                pCtx->UsbDataInterface = iface;
            }
        }

        /* If we found a data interface, prefer it for pipes */
        if (pCtx->UsbDataInterface == NULL && pCtx->UsbControlInterface == NULL) {
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }

    } else {
        /*
         * Single-interface device (common for MTK BROM / Preloader).
         * The only interface carries both the bulk endpoints and any
         * optional interrupt endpoint.
         */
        WDF_USB_DEVICE_SELECT_CONFIG_PARAMS_INIT_SINGLE_INTERFACE(
            &cfgParams);

        status = WdfUsbTargetDeviceSelectConfig(
            pCtx->UsbDevice,
            WDF_NO_OBJECT_ATTRIBUTES,
            &cfgParams);
        if (!NT_SUCCESS(status)) {
            return status;
        }

        pCtx->UsbDataInterface =
            cfgParams.Types.SingleInterface.ConfiguredUsbInterface;
        pCtx->UsbControlInterface = pCtx->UsbDataInterface;
    }

    return STATUS_SUCCESS;
}

/* ----------------------------------------------------------------
 * MtkSerialConfigurePipes
 *
 * Walk configured pipes on the data interface (or single interface)
 * and locate:
 *   - Bulk IN  endpoint  (device → host)
 *   - Bulk OUT endpoint  (host → device)
 *   - Interrupt IN ep    (serial-state notifications, optional)
 *
 * Pipe policies are set to match the original usb2ser.sys behaviour:
 *   RAW_IO                   = TRUE
 *   SHORT_PACKET_TERMINATE   = FALSE
 *   PIPE_TRANSFER_TIMEOUT    = 5 000 ms
 *   MAXIMUM_TRANSFER_SIZE    = 1 MB
 * ---------------------------------------------------------------- */
NTSTATUS
MtkSerialConfigurePipes(
    _In_ WDFDEVICE Device
    )
{
    PSERIAL_DEVICE_CONTEXT      pCtx;
    WDFUSBINTERFACE             iface;
    WDF_USB_PIPE_INFORMATION    pipeInfo;
    WDFUSBPIPE                  pipe;
    UCHAR                       numPipes;
    UCHAR                       idx;
    WDF_USB_PIPE_INFORMATION    ctrlPipeInfo;
    UCHAR                       numCtrlPipes;
    UCHAR                       ci;

    PAGED_CODE();

    pCtx = SerialGetDeviceContext(Device);
    pCtx->BulkReadPipe  = NULL;
    pCtx->BulkWritePipe = NULL;
    pCtx->InterruptPipe = NULL;

    /* ---- Scan the data interface for bulk endpoints ---- */
    iface = pCtx->UsbDataInterface;
    if (iface == NULL) {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    numPipes = WdfUsbInterfaceGetNumConfiguredPipes(iface);

    for (idx = 0; idx < numPipes; idx++) {
        WDF_USB_PIPE_INFORMATION_INIT(&pipeInfo);
        pipe = WdfUsbInterfaceGetConfiguredPipe(iface, idx, &pipeInfo);

        if (pipeInfo.PipeType == WdfUsbPipeTypeBulk) {
            if (WdfUsbTargetPipeIsInEndpoint(pipe)) {
                pCtx->BulkReadPipe = pipe;
            } else {
                pCtx->BulkWritePipe = pipe;
            }
            WdfUsbTargetPipeSetNoMaximumPacketSizeCheck(pipe);

        } else if (pipeInfo.PipeType == WdfUsbPipeTypeInterrupt &&
                   WdfUsbTargetPipeIsInEndpoint(pipe)) {
            pCtx->InterruptPipe = pipe;
        }
    }

    /* ---- Also scan control interface if different ---- */
    if (pCtx->UsbControlInterface != NULL &&
        pCtx->UsbControlInterface != pCtx->UsbDataInterface) {

        numCtrlPipes = WdfUsbInterfaceGetNumConfiguredPipes(
            pCtx->UsbControlInterface);

        for (ci = 0; ci < numCtrlPipes; ci++) {
            WDF_USB_PIPE_INFORMATION_INIT(&ctrlPipeInfo);
            pipe = WdfUsbInterfaceGetConfiguredPipe(
                pCtx->UsbControlInterface, ci, &ctrlPipeInfo);

            if (ctrlPipeInfo.PipeType == WdfUsbPipeTypeInterrupt &&
                WdfUsbTargetPipeIsInEndpoint(pipe) &&
                pCtx->InterruptPipe == NULL) {
                pCtx->InterruptPipe = pipe;
            }
        }
    }

    if (pCtx->BulkReadPipe == NULL || pCtx->BulkWritePipe == NULL) {
        return STATUS_INVALID_DEVICE_STATE;
    }

    return STATUS_SUCCESS;
}

/* ----------------------------------------------------------------
 * MtkSerialRegisterComPort
 *
 * Write a value to HKLM\HARDWARE\DEVICEMAP\SERIALCOMM so that
 * legacy applications discover the virtual COM port (\\.\COMx).
 * ---------------------------------------------------------------- */
NTSTATUS
MtkSerialRegisterComPort(
    _In_ WDFDEVICE Device
    )
{
    PSERIAL_DEVICE_CONTEXT  pCtx;
    OBJECT_ATTRIBUTES       oa;
    UNICODE_STRING          keyPath;
    HANDLE                  hKey = NULL;
    NTSTATUS                status;

    PAGED_CODE();

    pCtx = SerialGetDeviceContext(Device);
    if (pCtx->ComPortRegistered) {
        return STATUS_SUCCESS;
    }

    RtlInitUnicodeString(&keyPath, MTK_SERIALCOMM_PATH);
    InitializeObjectAttributes(&oa, &keyPath,
        OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

    status = ZwCreateKey(
        &hKey, KEY_SET_VALUE, &oa, 0, NULL,
        REG_OPTION_VOLATILE, NULL);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    /* Value name = device name, value data = COM port name */
    status = ZwSetValueKey(
        hKey,
        &pCtx->DeviceName,
        0,
        REG_SZ,
        pCtx->ComPortName,
        (ULONG)(wcslen(pCtx->ComPortName) + 1) * sizeof(WCHAR));

    ZwClose(hKey);

    if (NT_SUCCESS(status)) {
        pCtx->ComPortRegistered = TRUE;
    }

    return status;
}

/* ----------------------------------------------------------------
 * MtkSerialUnregisterComPort — Remove SERIALCOMM registry entry
 * ---------------------------------------------------------------- */
VOID
MtkSerialUnregisterComPort(
    _In_ WDFDEVICE Device
    )
{
    PSERIAL_DEVICE_CONTEXT  pCtx;
    OBJECT_ATTRIBUTES       oa;
    UNICODE_STRING          keyPath;
    HANDLE                  hKey = NULL;
    NTSTATUS                status;

    PAGED_CODE();

    pCtx = SerialGetDeviceContext(Device);
    if (!pCtx->ComPortRegistered) {
        return;
    }

    RtlInitUnicodeString(&keyPath, MTK_SERIALCOMM_PATH);
    InitializeObjectAttributes(&oa, &keyPath,
        OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

    status = ZwOpenKey(&hKey, KEY_SET_VALUE, &oa);
    if (NT_SUCCESS(status)) {
        ZwDeleteValueKey(hKey, &pCtx->DeviceName);
        ZwClose(hKey);
    }

    pCtx->ComPortRegistered = FALSE;
}
