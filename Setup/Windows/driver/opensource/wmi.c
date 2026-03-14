/*
 * wmi.c
 *
 * WMI registration for serial port name and comm data instances.
 * Implements the standard serial port WMI GUIDs used by Device Manager
 * and serial port enumeration utilities.
 *
 * SPDX-License-Identifier: MIT
 */

#include "mtk_usb2ser.h"

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, WmiRegistration)
#endif

EVT_WDF_WMI_INSTANCE_QUERY_INSTANCE EvtWmiInstanceQueryPortName;
EVT_WDF_WMI_INSTANCE_QUERY_INSTANCE EvtWmiInstanceQueryCommData;

NTSTATUS
EvtWmiInstanceQueryPortName(
    _In_  WDFWMIINSTANCE WmiInstance,
    _In_  ULONG          OutBufferSize,
    _Out_writes_bytes_to_(OutBufferSize, *BufferUsed) PVOID OutBuffer,
    _Out_ PULONG         BufferUsed
    )
{
    WDFDEVICE       device = WdfWmiInstanceGetDevice(WmiInstance);
    PDEVICE_CONTEXT devCtx = GetDeviceContext(device);
    USHORT          nameLength;
    ULONG           requiredSize;

    nameLength = devCtx->PortName.Length;
    requiredSize = sizeof(USHORT) + nameLength;

    if (OutBufferSize < requiredSize) {
        *BufferUsed = requiredSize;
        return STATUS_BUFFER_TOO_SMALL;
    }

    /* WMI string format: USHORT length prefix followed by WCHAR data */
    *(PUSHORT)OutBuffer = nameLength;
    if (nameLength > 0) {
        RtlCopyMemory((PUCHAR)OutBuffer + sizeof(USHORT),
                       devCtx->PortName.Buffer,
                       nameLength);
    }

    *BufferUsed = requiredSize;
    return STATUS_SUCCESS;
}

NTSTATUS
EvtWmiInstanceQueryCommData(
    _In_  WDFWMIINSTANCE WmiInstance,
    _In_  ULONG          OutBufferSize,
    _Out_writes_bytes_to_(OutBufferSize, *BufferUsed) PVOID OutBuffer,
    _Out_ PULONG         BufferUsed
    )
{
    WDFDEVICE       device = WdfWmiInstanceGetDevice(WmiInstance);
    PDEVICE_CONTEXT devCtx = GetDeviceContext(device);
    ULONG           requiredSize;

    /*
     * The SERIAL_WMI_COMM_DATA structure contains:
     *   BaudRate, BitsPerByte, Parity, StopBits, XoffCharacter,
     *   XoffXmitThreshold, XonCharacter, XonXmitThreshold,
     *   MaximumBaudRate, MaximumOutputBufferSize, MaximumInputBufferSize,
     *   Support16BitMode, SupportDTRDSR, SupportIntervalTimeouts,
     *   SupportParityCheck, SupportRTSCTS, SupportXonXoff,
     *   SettableBaudRate, SettableDataBits, SettableFlowControl,
     *   SettableParity, SettableParityCheck, SettableStopAndParityBits.
     *
     * For simplicity, return baud rate and data bits at minimum.
     */
    requiredSize = sizeof(ULONG) * 2;

    if (OutBufferSize < requiredSize) {
        *BufferUsed = requiredSize;
        return STATUS_BUFFER_TOO_SMALL;
    }

    RtlZeroMemory(OutBuffer, OutBufferSize);

    /* BaudRate */
    ((PULONG)OutBuffer)[0] = devCtx->LineCoding.dwDTERate;
    /* BitsPerByte */
    ((PULONG)OutBuffer)[1] = devCtx->LineCoding.bDataBits;

    *BufferUsed = requiredSize;
    return STATUS_SUCCESS;
}

NTSTATUS
WmiRegistration(
    _In_ WDFDEVICE Device
    )
{
    NTSTATUS                    status;
    WDF_WMI_PROVIDER_CONFIG     providerConfig;
    WDF_WMI_INSTANCE_CONFIG     instanceConfig;

    PAGED_CODE();

    /* Register serial port name WMI instance */
    WDF_WMI_PROVIDER_CONFIG_INIT(&providerConfig,
                                  &MTK_SERIAL_PORT_WMI_NAME_GUID);
    providerConfig.MinInstanceBufferSize = sizeof(USHORT) + 64;

    WDF_WMI_INSTANCE_CONFIG_INIT_PROVIDER_CONFIG(
        &instanceConfig, &providerConfig);
    instanceConfig.Register           = TRUE;
    instanceConfig.EvtWmiInstanceQueryInstance = EvtWmiInstanceQueryPortName;

    status = WdfWmiInstanceCreate(Device, &instanceConfig,
                                  WDF_NO_OBJECT_ATTRIBUTES, WDF_NO_HANDLE);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    /* Register serial port comm data WMI instance */
    WDF_WMI_PROVIDER_CONFIG_INIT(&providerConfig,
                                  &MTK_SERIAL_PORT_WMI_COMM_GUID);
    providerConfig.MinInstanceBufferSize = sizeof(ULONG) * 2;

    WDF_WMI_INSTANCE_CONFIG_INIT_PROVIDER_CONFIG(
        &instanceConfig, &providerConfig);
    instanceConfig.Register           = TRUE;
    instanceConfig.EvtWmiInstanceQueryInstance = EvtWmiInstanceQueryCommData;

    status = WdfWmiInstanceCreate(Device, &instanceConfig,
                                  WDF_NO_OBJECT_ATTRIBUTES, WDF_NO_HANDLE);

    return status;
}
