#!/usr/bin/python3
# -*- coding: utf-8 -*-
# Python ctypes bridge for the native mtk_usb_driver DLL
# Provides device detection, reset, and recovery on Windows 11 x64
import os
import sys
import ctypes
import ctypes.util
import logging
from ctypes import (
    Structure, c_uint16, c_uint8, c_int, c_char, c_char_p,
    POINTER, byref
)

logger = logging.getLogger(__name__)


class MtkDeviceInfo(Structure):
    """Mirrors mtk_device_info_t from mtk_usb_driver.h"""
    _fields_ = [
        ("vid", c_uint16),
        ("pid", c_uint16),
        ("is_preloader", c_int),
        ("is_bootrom", c_int),
        ("is_da_mode", c_int),
        ("device_path", c_char * 512),
        ("description", c_char * 256),
        ("driver_name", c_char * 128),
    ]


# Error codes matching the C header
MTK_SUCCESS = 0
MTK_ERROR_NOT_FOUND = -1
MTK_ERROR_ACCESS = -2
MTK_ERROR_IO = -3
MTK_ERROR_TIMEOUT = -4
MTK_ERROR_PIPE = -5
MTK_ERROR_NO_DRIVER = -6
MTK_ERROR_INVALID_PARAM = -7
MTK_ERROR_NO_MEMORY = -8
MTK_ERROR_NOT_SUPPORTED = -9
MTK_ERROR_OTHER = -99

MTK_MAX_DEVICES = 16


class MtkUsbDriver:
    """
    Python wrapper for the native mtk_usb_driver DLL.

    Provides:
    - Device enumeration via Windows SetupAPI (more reliable than libusb on Win11)
    - USB device reset without physical disconnect
    - Endpoint clear/halt recovery for I/O errors
    - USB selective suspend management
    """

    def __init__(self, log_level=logging.INFO):
        self._lib = None
        self._loaded = False
        self._log_level = log_level

        if not sys.platform.startswith('win32'):
            logger.debug("MtkUsbDriver: Not on Windows, native driver not available")
            return

        self._load_library()

    def _load_library(self):
        """Load the native DLL from the Windows directory."""
        try:
            # Search paths for the DLL
            search_dirs = [
                os.path.join(os.path.dirname(__file__), "..", "..", "Windows"),
                os.path.join(os.path.dirname(__file__), "native"),
                os.path.dirname(__file__),
            ]

            dll_name = "mtk_usb_driver.dll"

            for search_dir in search_dirs:
                dll_path = os.path.join(os.path.abspath(search_dir), dll_name)
                if os.path.exists(dll_path):
                    try:
                        self._lib = ctypes.CDLL(dll_path)
                        self._setup_prototypes()
                        self._loaded = True
                        logger.info(f"Loaded native USB driver from: {dll_path}")

                        # Initialize the library
                        rc = self._lib.mtk_usb_init()
                        if rc != MTK_SUCCESS:
                            logger.warning(f"mtk_usb_init returned: {rc}")

                        return
                    except OSError as e:
                        logger.debug(f"Failed to load {dll_path}: {e}")
                        continue

            logger.debug(f"Native USB driver DLL not found in search paths")

        except Exception as e:
            logger.debug(f"Failed to load native USB driver: {e}")

    def _setup_prototypes(self):
        """Set up ctypes function prototypes for type safety."""
        lib = self._lib

        lib.mtk_usb_init.restype = c_int
        lib.mtk_usb_init.argtypes = []

        lib.mtk_usb_cleanup.restype = None
        lib.mtk_usb_cleanup.argtypes = []

        lib.mtk_usb_find_devices.restype = c_int
        lib.mtk_usb_find_devices.argtypes = [POINTER(MtkDeviceInfo), c_int]

        lib.mtk_usb_wait_for_device.restype = c_int
        lib.mtk_usb_wait_for_device.argtypes = [POINTER(MtkDeviceInfo), c_int, c_int]

        lib.mtk_usb_reset_device.restype = c_int
        lib.mtk_usb_reset_device.argtypes = [c_uint16, c_uint16]

        lib.mtk_usb_reset_device_by_path.restype = c_int
        lib.mtk_usb_reset_device_by_path.argtypes = [c_char_p]

        lib.mtk_usb_clear_endpoint.restype = c_int
        lib.mtk_usb_clear_endpoint.argtypes = [c_char_p, c_uint8]

        lib.mtk_usb_disable_selective_suspend.restype = c_int
        lib.mtk_usb_disable_selective_suspend.argtypes = [c_uint16, c_uint16]

        lib.mtk_usb_get_driver_name.restype = c_int
        lib.mtk_usb_get_driver_name.argtypes = [c_uint16, c_uint16, c_char_p, c_int]

        lib.mtk_usb_check_driver_installed.restype = c_int
        lib.mtk_usb_check_driver_installed.argtypes = [c_uint16, c_uint16]

        lib.mtk_usb_error_string.restype = c_char_p
        lib.mtk_usb_error_string.argtypes = [c_int]

        lib.mtk_usb_version.restype = c_char_p
        lib.mtk_usb_version.argtypes = []

        lib.mtk_usb_set_log.restype = c_int
        lib.mtk_usb_set_log.argtypes = [c_char_p, c_int]

    @property
    def available(self):
        """Check if the native driver is loaded and available."""
        return self._loaded

    def find_devices(self):
        """
        Find all connected MediaTek USB devices.
        Returns list of device info dicts.
        """
        if not self._loaded:
            return []

        devices = (MtkDeviceInfo * MTK_MAX_DEVICES)()
        count = self._lib.mtk_usb_find_devices(devices, MTK_MAX_DEVICES)

        if count < 0:
            logger.error(f"mtk_usb_find_devices error: {self.error_string(count)}")
            return []

        result = []
        for i in range(count):
            dev = devices[i]
            result.append({
                'vid': dev.vid,
                'pid': dev.pid,
                'is_preloader': bool(dev.is_preloader),
                'is_bootrom': bool(dev.is_bootrom),
                'is_da_mode': bool(dev.is_da_mode),
                'device_path': dev.device_path.decode('utf-8', errors='replace'),
                'description': dev.description.decode('utf-8', errors='replace'),
                'driver_name': dev.driver_name.decode('utf-8', errors='replace'),
            })

        return result

    def wait_for_device(self, timeout_ms=30000, poll_interval_ms=500):
        """
        Wait for a MediaTek device to appear.
        Returns device info dict or None on timeout.
        """
        if not self._loaded:
            return None

        device = MtkDeviceInfo()
        rc = self._lib.mtk_usb_wait_for_device(byref(device),
                                                 timeout_ms,
                                                 poll_interval_ms)
        if rc == MTK_SUCCESS:
            return {
                'vid': device.vid,
                'pid': device.pid,
                'is_preloader': bool(device.is_preloader),
                'is_bootrom': bool(device.is_bootrom),
                'device_path': device.device_path.decode('utf-8', errors='replace'),
                'description': device.description.decode('utf-8', errors='replace'),
                'driver_name': device.driver_name.decode('utf-8', errors='replace'),
            }

        return None

    def reset_device(self, vid, pid):
        """
        Reset USB device without physical disconnect.
        Returns True on success.
        """
        if not self._loaded:
            return False

        rc = self._lib.mtk_usb_reset_device(vid, pid)
        if rc != MTK_SUCCESS:
            logger.warning(f"Device reset failed: {self.error_string(rc)}")
        return rc == MTK_SUCCESS

    def clear_endpoint(self, device_path, endpoint):
        """
        Clear a stalled/halted endpoint to recover from I/O errors.
        Returns True on success.
        """
        if not self._loaded:
            return False

        if isinstance(device_path, str):
            device_path = device_path.encode('utf-8')

        rc = self._lib.mtk_usb_clear_endpoint(device_path, endpoint)
        if rc != MTK_SUCCESS:
            logger.debug(f"Endpoint clear failed: {self.error_string(rc)}")
        return rc == MTK_SUCCESS

    def disable_selective_suspend(self, vid, pid):
        """
        Disable USB selective suspend for the device.
        Prevents Windows power management from disconnecting the device.
        Returns True on success.
        """
        if not self._loaded:
            return False

        rc = self._lib.mtk_usb_disable_selective_suspend(vid, pid)
        return rc == MTK_SUCCESS

    def check_driver(self, vid, pid):
        """
        Check if a compatible USB driver is installed.
        Returns True if WinUSB/libusb/libusbK is installed.
        """
        if not self._loaded:
            return False

        rc = self._lib.mtk_usb_check_driver_installed(vid, pid)
        return rc == 1

    def get_driver_name(self, vid, pid):
        """Get the driver name bound to the device."""
        if not self._loaded:
            return ""

        buf = ctypes.create_string_buffer(128)
        rc = self._lib.mtk_usb_get_driver_name(vid, pid, buf, 128)
        if rc == MTK_SUCCESS:
            return buf.value.decode('utf-8', errors='replace')
        return ""

    def error_string(self, code):
        """Get human-readable error description."""
        if not self._loaded:
            return f"Native driver not loaded (code: {code})"
        result = self._lib.mtk_usb_error_string(code)
        if result:
            return result.decode('utf-8', errors='replace')
        return f"Unknown error ({code})"

    def version(self):
        """Get native driver version."""
        if not self._loaded:
            return "not loaded"
        result = self._lib.mtk_usb_version()
        if result:
            return result.decode('utf-8', errors='replace')
        return "unknown"

    def set_logging(self, log_file=None, verbose=False):
        """Enable/disable native driver logging."""
        if not self._loaded:
            return False

        if log_file is None:
            rc = self._lib.mtk_usb_set_log(None, 0)
        else:
            if isinstance(log_file, str):
                log_file = log_file.encode('utf-8')
            rc = self._lib.mtk_usb_set_log(log_file, 1 if verbose else 0)

        return rc == MTK_SUCCESS

    def cleanup(self):
        """Release all native resources."""
        if self._loaded:
            self._lib.mtk_usb_cleanup()


# Module-level singleton instance
_driver_instance = None


def get_native_driver():
    """Get the singleton native driver instance."""
    global _driver_instance
    if _driver_instance is None:
        _driver_instance = MtkUsbDriver()
    return _driver_instance
