#!/usr/bin/python3
# -*- coding: utf-8 -*-
# (c) B.Kerler 2018-2025 GPLv3 License
"""
Windows 11 USB device re-enumeration and detection utilities.

Provides helpers for:
- Forcing USB device re-enumeration via SetupAPI / cfgmgr32
- Waiting for a device to re-appear after disconnect
- Checking if a libusb-compatible driver (WinUSB/libusb-win32/libusbK) is installed
"""
import sys
import os
import time
import logging

logger = logging.getLogger(__name__)


def is_windows():
    return sys.platform.startswith('win') or os.name == 'nt'


def wait_for_usb_device(vid, pid, timeout=30, interval=0.5):
    """
    Poll for a USB device with given VID/PID to appear.
    Returns True if found within *timeout* seconds, False otherwise.

    Works on all platforms via pyusb; on Windows uses an
    additional SetupAPI / cfgmgr32 hint when available.
    """
    import usb.core
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            dev = usb.core.find(idVendor=vid, idProduct=pid)
            if dev is not None:
                return True
        except Exception:
            pass
        time.sleep(interval)
    return False


# ---------- Windows-only helpers (SetupAPI / cfgmgr32) ----------

def _try_reenumerate_device_windows():
    """
    Ask the Windows USB hub to re-enumerate all connected devices by
    calling CM_Reenumerate_DevNode on the root of the USB tree.

    This is the programmatic equivalent of "Scan for hardware changes"
    in Device Manager.  Returns True on success.
    """
    if not is_windows():
        return False
    try:
        import ctypes
        from ctypes import wintypes

        cfgmgr32 = ctypes.windll.cfgmgr32

        CR_SUCCESS = 0
        CM_REENUMERATE_NORMAL = 0x0
        CM_LOCATE_DEVNODE_NORMAL = 0x0

        devInst = wintypes.ULONG()
        # Locate the root device node (the top-level USB hub)
        ret = cfgmgr32.CM_Locate_DevNodeW(
            ctypes.byref(devInst),
            None,  # NULL => root
            CM_LOCATE_DEVNODE_NORMAL,
        )
        if ret != CR_SUCCESS:
            logger.debug(f"CM_Locate_DevNodeW failed: {ret}")
            return False

        ret = cfgmgr32.CM_Reenumerate_DevNode(devInst, CM_REENUMERATE_NORMAL)
        if ret != CR_SUCCESS:
            logger.debug(f"CM_Reenumerate_DevNode failed: {ret}")
            return False

        logger.debug("USB device re-enumeration triggered successfully")
        return True
    except Exception as e:
        logger.debug(f"Re-enumeration failed: {e}")
        return False


def reenumerate_and_wait(vid, pid, timeout=10):
    """
    On Windows, trigger a USB bus re-enumeration, then wait for the
    device with *vid*/*pid* to appear.
    On other platforms this is a no-op that simply waits.
    """
    if is_windows():
        _try_reenumerate_device_windows()
        # Give Windows time to process the re-enumeration
        time.sleep(1)
    return wait_for_usb_device(vid, pid, timeout=timeout)


def check_libusb_driver_installed():
    """
    Check whether a libusb-compatible driver is available on Windows.
    Returns a diagnostic string.
    """
    if not is_windows():
        return "Not Windows - N/A"

    try:
        import usb.backend.libusb1
        backend = usb.backend.libusb1.get_backend()
        if backend is not None:
            return "libusb1 backend available"
    except Exception:
        pass

    try:
        import usb.backend.libusb0
        backend = usb.backend.libusb0.get_backend()
        if backend is not None:
            return "libusb0 backend available (legacy)"
    except Exception:
        pass

    return ("No libusb backend found. Please install WinUSB or "
            "libusb-win32 driver using Zadig (https://zadig.akeo.ie)")
