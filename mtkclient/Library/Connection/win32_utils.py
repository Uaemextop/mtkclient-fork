#!/usr/bin/python3
# -*- coding: utf-8 -*-
# (c) B.Kerler 2018-2025 GPLv3 License
"""
Windows-specific USB utilities for mtkclient.

Provides native Windows USB device management using cfgmgr32 and SetupAPI,
removing the dependency on the third-party UsbDk driver.  Works with the
WinUSB driver installed by the mtkclient driver package.
"""

import logging
import sys
import time

logger = logging.getLogger(__name__)

# MediaTek USB Vendor ID and known Product IDs
MTK_VID = 0x0E8D
MTK_PIDS = {
    0x0003: "MTK Bootrom",
    0x2000: "MTK Preloader",
    0x2001: "MTK Preloader",
    0x20FF: "MTK Preloader",
    0x3000: "MTK Preloader",
    0x6000: "MTK Preloader",
}

if sys.platform == "win32":
    import ctypes
    from ctypes import wintypes

    # ------------------------------------------------------------------
    # cfgmgr32.dll – Configuration Manager API
    # ------------------------------------------------------------------
    try:
        _cfgmgr32 = ctypes.windll.cfgmgr32
    except OSError:
        _cfgmgr32 = None

    CR_SUCCESS = 0x00000000
    CM_REENUMERATE_NORMAL = 0x00000000
    CM_LOCATE_DEVNODE_NORMAL = 0x00000000

    # ------------------------------------------------------------------
    # setupapi.dll – SetupDi* helpers
    # ------------------------------------------------------------------
    try:
        _setupapi = ctypes.windll.setupapi
    except OSError:
        _setupapi = None

    DIGCF_PRESENT = 0x00000002
    DIGCF_ALLCLASSES = 0x00000004
    DIGCF_DEVICEINTERFACE = 0x00000010

    class SP_DEVINFO_DATA(ctypes.Structure):
        _fields_ = [
            ("cbSize", wintypes.DWORD),
            ("ClassGuid", ctypes.c_byte * 16),
            ("DevInst", wintypes.DWORD),
            ("Reserved", ctypes.POINTER(ctypes.c_ulong)),
        ]


def _is_available():
    """Return True if running on Windows with cfgmgr32 accessible."""
    return sys.platform == "win32" and _cfgmgr32 is not None


def reenumerate_usb_devices():
    """
    Ask Windows Configuration Manager to re-enumerate USB devices.

    This forces the OS to rescan the USB bus and rediscover devices,
    which is needed after a device reset (e.g. switching from preloader
    to download-agent mode).

    Returns True on success, False otherwise.
    """
    if not _is_available():
        return False

    try:
        dev_inst = wintypes.DWORD(0)
        # Locate the root device node
        ret = _cfgmgr32.CM_Locate_DevNodeW(
            ctypes.byref(dev_inst),
            None,
            CM_LOCATE_DEVNODE_NORMAL,
        )
        if ret != CR_SUCCESS:
            logger.debug("CM_Locate_DevNodeW failed: 0x%08X", ret)
            return False

        ret = _cfgmgr32.CM_Reenumerate_DevNode(
            dev_inst,
            CM_REENUMERATE_NORMAL,
        )
        if ret != CR_SUCCESS:
            logger.debug("CM_Reenumerate_DevNode failed: 0x%08X", ret)
            return False

        logger.debug("USB bus re-enumeration requested successfully")
        return True

    except Exception as exc:
        logger.debug("Re-enumeration failed: %s", exc)
        return False


def reenumerate_and_wait(delay=2.0):
    """Re-enumerate USB devices and wait for the OS to settle."""
    if reenumerate_usb_devices():
        time.sleep(delay)
        return True
    return False


def check_winusb_driver_installed():
    """
    Check if the mtkclient WinUSB driver is installed in the driver store.

    Looks for the WinUSB driver binding for MediaTek VID (0x0E8D) devices
    using the SetupAPI.

    Returns True if at least one matching device with WinUSB is found.
    """
    if not _is_available() or _setupapi is None:
        return False

    try:
        import subprocess
        result = subprocess.run(
            ["pnputil", "/enum-drivers"],
            capture_output=True, text=True, timeout=10,
        )
        if result.returncode == 0:
            output_lower = result.stdout.lower()
            if "mtkclient" in output_lower or "0e8d" in output_lower:
                return True
        return False
    except Exception as exc:
        logger.debug("Driver check failed: %s", exc)
        return False


def get_mtk_device_status():
    """
    Return a list of currently connected MediaTek USB devices and their
    driver status.

    Each entry is a dict:
        {"vid": int, "pid": int, "name": str, "driver": str | None}
    """
    devices = []
    if not _is_available():
        return devices

    try:
        import subprocess
        result = subprocess.run(
            ["pnputil", "/enum-devices", "/connected", "/ids"],
            capture_output=True, text=True, timeout=10,
        )
        if result.returncode != 0:
            return devices

        current = {}
        for line in result.stdout.splitlines():
            line = line.strip()
            if line.startswith("Instance ID:"):
                if current.get("instance_id"):
                    devices.append(current)
                current = {"instance_id": line.split(":", 1)[1].strip()}
            elif line.startswith("Hardware IDs:"):
                current["hwid"] = line.split(":", 1)[1].strip()
            elif line.startswith("Driver Name:"):
                current["driver"] = line.split(":", 1)[1].strip()
            elif line.startswith("Status:"):
                current["status"] = line.split(":", 1)[1].strip()

        if current.get("instance_id"):
            devices.append(current)

        # Filter to MediaTek devices
        mtk_devices = []
        for dev in devices:
            hwid = dev.get("hwid", "").upper()
            if "VID_0E8D" in hwid:
                for pid, name in MTK_PIDS.items():
                    pid_str = f"PID_{pid:04X}"
                    if pid_str in hwid:
                        mtk_devices.append({
                            "vid": MTK_VID,
                            "pid": pid,
                            "name": name,
                            "driver": dev.get("driver"),
                            "status": dev.get("status"),
                        })
                        break
        return mtk_devices

    except Exception as exc:
        logger.debug("Device status query failed: %s", exc)
        return devices


def suggest_driver_fix():
    """
    Return a user-friendly message suggesting how to fix driver issues.
    """
    msg = (
        "MTK device detected but no WinUSB driver is bound.\n"
        "To fix this, run the driver installer:\n"
        "  1. Open a Command Prompt as Administrator\n"
        "  2. Navigate to Setup\\Windows\\\n"
        "  3. Run: install_driver.bat\n"
        "\n"
        "Or install the MSI package:\n"
        "  Setup\\Windows\\output\\mtkclient_driver.msi\n"
        "\n"
        "After installation, reconnect your device."
    )
    return msg
