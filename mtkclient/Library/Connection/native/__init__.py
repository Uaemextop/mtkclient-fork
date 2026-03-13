#!/usr/bin/python3
# -*- coding: utf-8 -*-
# Python ctypes bridge for the native mtk_usb_driver DLL
# Provides device detection, reset, recovery, and WinUSB I/O on Windows
import os
import sys
import ctypes
import ctypes.util
import logging
from ctypes import (
    Structure, c_uint16, c_uint8, c_int, c_char, c_char_p,
    c_void_p, POINTER, byref
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


class MtkEndpointInfo(Structure):
    """Mirrors mtk_endpoint_info_t from mtk_usb_driver.h"""
    _fields_ = [
        ("ep_in", c_uint8),
        ("ep_out", c_uint8),
        ("max_packet_in", c_uint16),
        ("max_packet_out", c_uint16),
        ("interface_num", c_int),
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
    - WinUSB-based bulk I/O (no UsbDk/libusb required)
    - USB device reset without physical disconnect
    - Endpoint clear/halt recovery for I/O errors
    - USB selective suspend management
    - Control transfers for CDC device setup
    """

    def __init__(self, log_level=logging.INFO):
        self._lib = None
        self._loaded = False
        self._log_level = log_level
        self._handle = None  # Active WinUSB device handle

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

                        # Initialize the library (also loads WinUSB dynamically)
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

        # Lifecycle
        lib.mtk_usb_init.restype = c_int
        lib.mtk_usb_init.argtypes = []
        lib.mtk_usb_cleanup.restype = None
        lib.mtk_usb_cleanup.argtypes = []

        # Device enumeration
        lib.mtk_usb_find_devices.restype = c_int
        lib.mtk_usb_find_devices.argtypes = [POINTER(MtkDeviceInfo), c_int]
        lib.mtk_usb_wait_for_device.restype = c_int
        lib.mtk_usb_wait_for_device.argtypes = [POINTER(MtkDeviceInfo), c_int, c_int]

        # WinUSB device I/O
        lib.mtk_usb_open.restype = c_int
        lib.mtk_usb_open.argtypes = [c_uint16, c_uint16, POINTER(c_void_p)]
        lib.mtk_usb_open_by_path.restype = c_int
        lib.mtk_usb_open_by_path.argtypes = [c_char_p, POINTER(c_void_p)]
        lib.mtk_usb_close.restype = None
        lib.mtk_usb_close.argtypes = [c_void_p]
        lib.mtk_usb_get_endpoints.restype = c_int
        lib.mtk_usb_get_endpoints.argtypes = [c_void_p, POINTER(MtkEndpointInfo)]
        lib.mtk_usb_bulk_write.restype = c_int
        lib.mtk_usb_bulk_write.argtypes = [c_void_p, c_uint8, c_char_p, c_int, POINTER(c_int), c_int]
        lib.mtk_usb_bulk_read.restype = c_int
        lib.mtk_usb_bulk_read.argtypes = [c_void_p, c_uint8, c_char_p, c_int, POINTER(c_int), c_int]
        lib.mtk_usb_control_transfer.restype = c_int
        lib.mtk_usb_control_transfer.argtypes = [c_void_p, c_uint8, c_uint8, c_uint16, c_uint16, c_char_p, c_uint16, POINTER(c_int)]
        lib.mtk_usb_reset_pipe.restype = c_int
        lib.mtk_usb_reset_pipe.argtypes = [c_void_p, c_uint8]
        lib.mtk_usb_flush_pipe.restype = c_int
        lib.mtk_usb_flush_pipe.argtypes = [c_void_p, c_uint8]

        # Device management
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

        # Utility
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

    # ── Device enumeration ─────────────────────────────────

    def find_devices(self):
        """Find all connected MediaTek USB devices.
        Returns list of device info dicts."""
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
        """Wait for a MediaTek device to appear.
        Returns device info dict or None on timeout."""
        if not self._loaded:
            return None

        device = MtkDeviceInfo()
        rc = self._lib.mtk_usb_wait_for_device(byref(device), timeout_ms, poll_interval_ms)
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

    # ── WinUSB device I/O ──────────────────────────────────

    def open_device(self, vid, pid):
        """Open a WinUSB device for bulk I/O.
        Returns True on success."""
        if not self._loaded:
            return False

        handle = c_void_p()
        rc = self._lib.mtk_usb_open(vid, pid, byref(handle))
        if rc == MTK_SUCCESS and handle.value:
            self._handle = handle
            return True
        logger.debug(f"open_device failed: {self.error_string(rc)}")
        return False

    def close_device(self):
        """Close the current WinUSB device."""
        if self._loaded and self._handle:
            self._lib.mtk_usb_close(self._handle)
            self._handle = None

    def get_endpoints(self):
        """Get endpoint info for the open device.
        Returns dict with ep_in, ep_out, max_packet_in, max_packet_out."""
        if not self._loaded or not self._handle:
            return None

        ep_info = MtkEndpointInfo()
        rc = self._lib.mtk_usb_get_endpoints(self._handle, byref(ep_info))
        if rc == MTK_SUCCESS:
            return {
                'ep_in': ep_info.ep_in,
                'ep_out': ep_info.ep_out,
                'max_packet_in': ep_info.max_packet_in,
                'max_packet_out': ep_info.max_packet_out,
                'interface_num': ep_info.interface_num,
            }
        return None

    def bulk_write(self, data, endpoint=0, timeout_ms=5000):
        """Write data via WinUSB bulk OUT.
        Returns bytes written or -1 on error."""
        if not self._loaded or not self._handle:
            return -1

        if isinstance(data, (bytearray, memoryview)):
            data = bytes(data)

        actual = c_int(0)
        rc = self._lib.mtk_usb_bulk_write(
            self._handle, endpoint, data, len(data), byref(actual), timeout_ms
        )
        if rc == MTK_SUCCESS:
            return actual.value
        logger.debug(f"bulk_write failed: {self.error_string(rc)}")
        return -1

    def bulk_read(self, length, endpoint=0, timeout_ms=5000):
        """Read data via WinUSB bulk IN.
        Returns bytes read or empty bytes on error."""
        if not self._loaded or not self._handle:
            return b""

        buf = ctypes.create_string_buffer(length)
        actual = c_int(0)
        rc = self._lib.mtk_usb_bulk_read(
            self._handle, endpoint, buf, length, byref(actual), timeout_ms
        )
        if rc == MTK_SUCCESS:
            return buf.raw[:actual.value]
        return b""

    def control_transfer(self, request_type, request, value=0, index=0, data_or_length=None):
        """USB control transfer via WinUSB.
        Returns bytes transferred or -1 on error."""
        if not self._loaded or not self._handle:
            return -1

        actual = c_int(0)
        is_in = bool(request_type & 0x80)

        if is_in:
            length = data_or_length if isinstance(data_or_length, int) else 0
            buf = ctypes.create_string_buffer(length) if length > 0 else None
            rc = self._lib.mtk_usb_control_transfer(
                self._handle, request_type, request, value, index,
                buf, length, byref(actual)
            )
            if rc == MTK_SUCCESS:
                return buf.raw[:actual.value] if buf else b""
        else:
            if data_or_length and not isinstance(data_or_length, int):
                data = bytes(data_or_length)
                buf = ctypes.create_string_buffer(data)
                rc = self._lib.mtk_usb_control_transfer(
                    self._handle, request_type, request, value, index,
                    buf, len(data), byref(actual)
                )
            else:
                rc = self._lib.mtk_usb_control_transfer(
                    self._handle, request_type, request, value, index,
                    None, 0, byref(actual)
                )
            if rc == MTK_SUCCESS:
                return actual.value

        return -1

    def reset_pipe(self, endpoint=0):
        """Reset a WinUSB pipe (clear stall)."""
        if not self._loaded or not self._handle:
            return False
        return self._lib.mtk_usb_reset_pipe(self._handle, endpoint) == MTK_SUCCESS

    # ── Device management ──────────────────────────────────

    def reset_device(self, vid, pid):
        """Reset USB device without physical disconnect.
        Returns True on success."""
        if not self._loaded:
            return False
        rc = self._lib.mtk_usb_reset_device(vid, pid)
        if rc != MTK_SUCCESS:
            logger.warning(f"Device reset failed: {self.error_string(rc)}")
        return rc == MTK_SUCCESS

    def clear_endpoint(self, device_path, endpoint):
        """Clear a stalled/halted endpoint.
        Returns True on success."""
        if not self._loaded:
            return False
        if isinstance(device_path, str):
            device_path = device_path.encode('utf-8')
        rc = self._lib.mtk_usb_clear_endpoint(device_path, endpoint)
        return rc == MTK_SUCCESS

    def disable_selective_suspend(self, vid, pid):
        """Disable USB selective suspend for the device."""
        if not self._loaded:
            return False
        return self._lib.mtk_usb_disable_selective_suspend(vid, pid) == MTK_SUCCESS

    def check_driver(self, vid, pid):
        """Check if a compatible USB driver is installed."""
        if not self._loaded:
            return False
        return self._lib.mtk_usb_check_driver_installed(vid, pid) == 1

    def get_driver_name(self, vid, pid):
        """Get the driver name bound to the device."""
        if not self._loaded:
            return ""
        buf = ctypes.create_string_buffer(128)
        rc = self._lib.mtk_usb_get_driver_name(vid, pid, buf, 128)
        if rc == MTK_SUCCESS:
            return buf.value.decode('utf-8', errors='replace')
        return ""

    # ── Utility ────────────────────────────────────────────

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
        if self._handle:
            self.close_device()
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
