/*
 * power.c
 *
 * Power management - selective suspend / idle settings via KMDF.
 *
 * SPDX-License-Identifier: MIT
 */

#include "mtk_usb2ser.h"

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, PowerConfigureIdleSettings)
#endif

NTSTATUS
PowerConfigureIdleSettings(
    _In_ PDEVICE_CONTEXT DevCtx
    )
{
    NTSTATUS                        status;
    WDF_DEVICE_POWER_POLICY_IDLE_SETTINGS idleSettings;

    PAGED_CODE();

    if (!DevCtx->IdleEnabled) {
        return STATUS_SUCCESS;
    }

    WDF_DEVICE_POWER_POLICY_IDLE_SETTINGS_INIT(
        &idleSettings,
        IdleUsbSelectiveSuspend
        );

    idleSettings.IdleTimeout = DevCtx->IdleTimeSeconds * 1000;
    idleSettings.UserControlOfIdleSettings = IdleAllowUserControl;
    idleSettings.Enabled = WdfTrue;

    status = WdfDeviceAssignS0IdleSettings(DevCtx->Device, &idleSettings);

    if (status == STATUS_INVALID_DEVICE_REQUEST) {
        status = STATUS_SUCCESS;
    }

    return status;
}
