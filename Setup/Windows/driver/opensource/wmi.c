/*
 * wmi.c
 *
 * WMI registration for serial port data classes.
 * KMDF handles most WMI plumbing automatically.
 *
 * SPDX-License-Identifier: MIT
 */

#include "mtk_usb2ser.h"

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, WmiRegistration)
#endif

/* WMI query callback for serial port name */
static
NTSTATUS
EvtWmiInstanceQueryPortName(
    _In_  WDFWMIINSTANCE WmiInstance,
    _In_  ULONG          OutBufferSize,
    _Out_ PVOID          OutBuffer,
    _Out_ PULONG         BufferUsed
    )
{
    WDFDEVICE       device = WdfWmiInstanceGetDevice(WmiInstance);
    PDEVICE_CONTEXT devCtx = GetDeviceContext(device);
    USHORT          nameLen;
    ULONG           requiredSize;

    nameLen = devCtx->PortName.Length;
    requiredSize = sizeof(USHORT) + nameLen;

    if (OutBufferSize < requiredSize) {
        *BufferUsed = requiredSize;
        return STATUS_BUFFER_TOO_SMALL;
    }

    /* WMI string format: USHORT length + WCHAR[] data (no null terminator) */
    *(PUSHORT)OutBuffer = nameLen;
    if (nameLen > 0) {
        RtlCopyMemory((PUCHAR)OutBuffer + sizeof(USHORT),
                       devCtx->PortName.Buffer,
                       nameLen);
    }

    *BufferUsed = requiredSize;
    return STATUS_SUCCESS;
}

/* WMI query callback for serial port comm data */
static
NTSTATUS
EvtWmiInstanceQueryCommData(
    _In_  WDFWMIINSTANCE WmiInstance,
    _In_  ULONG          OutBufferSize,
    _Out_ PVOID          OutBuffer,
    _Out_ PULONG         BufferUsed
    )
{
    WDFDEVICE       device = WdfWmiInstanceGetDevice(WmiInstance);
    PDEVICE_CONTEXT devCtx = GetDeviceContext(device);
    ULONG           requiredSize;
    PUCHAR          buf;

    /*
     * MSSerial_CommInfo structure (simplified):
     *   ULONG BaudRate
     *   ULONG BitsPerByte
     *   ULONG Parity
     *   ULONG StopBits
     *   ULONG XoffCharacter
     *   ULONG XonCharacter
     *   ULONG MaximumBaudRate
     *   ULONG MaximumOutputBufferSize
     *   ULONG MaximumInputBufferSize
     *   BOOLEAN Support16BitMode
     *   BOOLEAN SupportDTRDSR
     *   BOOLEAN SupportIntervalTimeouts
     *   BOOLEAN SupportParityCheck
     *   BOOLEAN SupportRTSCTS
     *   BOOLEAN SupportXonXoff
     *   BOOLEAN SettableBaudRate
     *   BOOLEAN SettableDataBits
     *   BOOLEAN SettableFlowControl
     *   BOOLEAN SettableParity
     *   BOOLEAN SettableParityCheck
     *   BOOLEAN SettableStopBits
     *   BOOLEAN IsBusy
     */
    requiredSize = 23 * sizeof(ULONG);

    if (OutBufferSize < requiredSize) {
        *BufferUsed = requiredSize;
        return STATUS_BUFFER_TOO_SMALL;
    }

    buf = (PUCHAR)OutBuffer;
    RtlZeroMemory(buf, requiredSize);

    /* BaudRate */
    *(PULONG)(buf + 0) = devCtx->LineCoding.dwDTERate;
    /* BitsPerByte */
    *(PULONG)(buf + 4) = devCtx->LineCoding.bDataBits;
    /* MaximumBaudRate */
    *(PULONG)(buf + 24) = MAX_BAUD_RATE;
    /* MaximumOutputBufferSize */
    *(PULONG)(buf + 28) = devCtx->OutQueueSize;
    /* MaximumInputBufferSize */
    *(PULONG)(buf + 32) = devCtx->InQueueSize;

    *BufferUsed = requiredSize;
    return STATUS_SUCCESS;
}

/* =========================================================================
 *  WmiRegistration — register WMI data blocks for serial port
 * ========================================================================= */
NTSTATUS
WmiRegistration(
    _In_ WDFDEVICE Device
    )
{
    NTSTATUS                        status;
    WDF_WMI_PROVIDER_CONFIG         providerConfig;
    WDF_WMI_INSTANCE_CONFIG         instanceConfig;
    WDFWMIINSTANCE                  wmiInstance;

    PAGED_CODE();

    /* Register serial port name WMI instance */
    WDF_WMI_PROVIDER_CONFIG_INIT(&providerConfig,
                                  &MTK_SERIAL_PORT_WMI_NAME_GUID);
    providerConfig.MinInstanceBufferSize = sizeof(USHORT) + 64;

    WDF_WMI_INSTANCE_CONFIG_INIT_PROVIDER_CONFIG(
        &instanceConfig, &providerConfig);
    instanceConfig.Register             = TRUE;
    instanceConfig.EvtWmiInstanceQueryInstance = EvtWmiInstanceQueryPortName;

    status = WdfWmiInstanceCreate(Device, &instanceConfig,
                                  WDF_NO_OBJECT_ATTRIBUTES, &wmiInstance);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    /* Register serial port comm data WMI instance */
    WDF_WMI_PROVIDER_CONFIG_INIT(&providerConfig,
                                  &MTK_SERIAL_PORT_WMI_COMM_GUID);
    providerConfig.MinInstanceBufferSize = 23 * sizeof(ULONG);

    WDF_WMI_INSTANCE_CONFIG_INIT_PROVIDER_CONFIG(
        &instanceConfig, &providerConfig);
    instanceConfig.Register             = TRUE;
    instanceConfig.EvtWmiInstanceQueryInstance = EvtWmiInstanceQueryCommData;

    status = WdfWmiInstanceCreate(Device, &instanceConfig,
                                  WDF_NO_OBJECT_ATTRIBUTES, &wmiInstance);

    return status;
}
