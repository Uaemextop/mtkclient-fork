#!/usr/bin/python3
# -*- coding: utf-8 -*-
# (c) 2024-2026 GPLv3 License
#
# win32_utils.py - Windows WinUSB utility module for mtkclient
#
# Provides direct USB device access via WinUSB API (ctypes bindings),
# eliminating the need for UsbDk and libusb on Windows.
#
# Architecture:
#   Application -> win32_utils.py (ctypes) -> winusb.dll -> WinUSB.sys -> USB Hardware
#
# Falls back gracefully: if WinUSB is not available, usblib.py will use libusb.

import os
import sys
import logging
import time
import ctypes
import ctypes.wintypes as wintypes
from ctypes import (
    Structure, Union, POINTER, byref, sizeof, cast, create_string_buffer,
    c_void_p, c_int, c_uint8, c_uint16, c_uint32, c_ulong, c_ubyte,
    c_bool, c_char, c_wchar_p,
)

logger = logging.getLogger(__name__)

# Only available on Windows
if not sys.platform.startswith('win32'):
    raise ImportError("win32_utils is only available on Windows")

import winreg  # noqa: E402 - Windows-only module, imported after platform check

# ── Windows constants ──────────────────────────────────────────

INVALID_HANDLE_VALUE = c_void_p(-1).value
GENERIC_READ = 0x80000000
GENERIC_WRITE = 0x40000000
FILE_SHARE_READ = 0x00000001
FILE_SHARE_WRITE = 0x00000002
OPEN_EXISTING = 3
FILE_ATTRIBUTE_NORMAL = 0x80
FILE_FLAG_OVERLAPPED = 0x40000000

DIGCF_PRESENT = 0x00000002
DIGCF_DEVICEINTERFACE = 0x00000010
SPDRP_DEVICEDESC = 0x00000000
SPDRP_SERVICE = 0x00000004

ERROR_NO_MORE_ITEMS = 259
ERROR_INSUFFICIENT_BUFFER = 122

# WinUSB pipe policies
PIPE_TRANSFER_TIMEOUT = 0x03
RAW_IO = 0x07
AUTO_CLEAR_STALL = 0x02
IGNORE_SHORT_PACKETS = 0x04

# USB endpoint directions
USB_ENDPOINT_DIRECTION_IN = 0x80
USB_ENDPOINT_DIRECTION_OUT = 0x00

# WinUSB pipe types
UsbdPipeTypeBulk = 0x02

# CM_Reenumerate flags
CM_REENUMERATE_RETRY_INSTALLATION = 0x00000002

# DICS flags for device state changes
DIF_PROPERTYCHANGE = 0x00000012
DICS_ENABLE = 0x00000001
DICS_DISABLE = 0x00000002
DICS_FLAG_CONFIGSPECIFIC = 0x00000002

# DeviceInterfaceGUID matching the WinUSB INF
MTKCLIENT_DEVICE_GUID = "{1D0C3B4F-2E1A-4A32-9C3F-5D6B7E8F9A0B}"
# Standard USB device interface GUID (fallback)
GUID_DEVINTERFACE_USB_DEVICE = "{A5DCBF10-6530-11D2-901F-00C04FB951ED}"

# ── Windows structures ─────────────────────────────────────────


class GUID(Structure):
    _fields_ = [
        ("Data1", c_ulong),
        ("Data2", c_uint16),
        ("Data3", c_uint16),
        ("Data4", c_ubyte * 8),
    ]


class SP_DEVINFO_DATA(Structure):
    _fields_ = [
        ("cbSize", c_ulong),
        ("ClassGuid", GUID),
        ("DevInst", c_ulong),
        ("Reserved", POINTER(c_ulong)),
    ]


class SP_DEVICE_INTERFACE_DATA(Structure):
    _fields_ = [
        ("cbSize", c_ulong),
        ("InterfaceClassGuid", GUID),
        ("Flags", c_ulong),
        ("Reserved", POINTER(c_ulong)),
    ]


class SP_DEVICE_INTERFACE_DETAIL_DATA_W(Structure):
    _fields_ = [
        ("cbSize", c_ulong),
        ("DevicePath", c_wchar_p),
    ]


class SP_CLASSINSTALL_HEADER(Structure):
    _fields_ = [
        ("cbSize", c_ulong),
        ("InstallFunction", c_ulong),
    ]


class SP_PROPCHANGE_PARAMS(Structure):
    _fields_ = [
        ("ClassInstallHeader", SP_CLASSINSTALL_HEADER),
        ("StateChange", c_ulong),
        ("Scope", c_ulong),
        ("HwProfile", c_ulong),
    ]


class WINUSB_SETUP_PACKET(Structure):
    _fields_ = [
        ("RequestType", c_ubyte),
        ("Request", c_ubyte),
        ("Value", c_uint16),
        ("Index", c_uint16),
        ("Length", c_uint16),
    ]


class USB_INTERFACE_DESCRIPTOR(Structure):
    _fields_ = [
        ("bLength", c_ubyte),
        ("bDescriptorType", c_ubyte),
        ("bInterfaceNumber", c_ubyte),
        ("bAlternateSetting", c_ubyte),
        ("bNumEndpoints", c_ubyte),
        ("bInterfaceClass", c_ubyte),
        ("bInterfaceSubClass", c_ubyte),
        ("bInterfaceProtocol", c_ubyte),
        ("iInterface", c_ubyte),
    ]


class WINUSB_PIPE_INFORMATION(Structure):
    _fields_ = [
        ("PipeType", c_int),
        ("PipeId", c_ubyte),
        ("MaximumPacketSize", c_uint16),
        ("Interval", c_ubyte),
    ]


# ── GUID parsing ──────────────────────────────────────────────


def parse_guid(guid_str):
    """Parse a GUID string like '{1D0C3B4F-2E1A-4A32-9C3F-5D6B7E8F9A0B}' to GUID struct."""
    s = guid_str.strip('{}')
    parts = s.split('-')
    g = GUID()
    g.Data1 = int(parts[0], 16)
    g.Data2 = int(parts[1], 16)
    g.Data3 = int(parts[2], 16)
    d4 = parts[3] + parts[4]
    for i in range(8):
        g.Data4[i] = int(d4[i * 2:i * 2 + 2], 16)
    return g


# ── DLL loading ───────────────────────────────────────────────

_kernel32 = ctypes.windll.kernel32
_setupapi = None
_cfgmgr32 = None
_winusb = None


def _load_dlls():
    """Load Windows system DLLs."""
    global _setupapi, _cfgmgr32, _winusb

    if _setupapi is not None:
        return _winusb is not None

    try:
        _setupapi = ctypes.windll.setupapi
        _cfgmgr32 = ctypes.windll.cfgmgr32
    except OSError as e:
        logger.error(f"Failed to load SetupAPI/CfgMgr32: {e}")
        return False

    try:
        _winusb = ctypes.windll.winusb
        logger.info("WinUSB loaded successfully")
        return True
    except OSError:
        logger.warning("WinUSB not available - device may need WinUSB driver installed")
        return False


def winusb_available():
    """Check if WinUSB API is available on this system."""
    return _load_dlls()


# ── SetupAPI device enumeration ───────────────────────────────


def _find_device_path(vid, pid, guid_str=None):
    """Find device path for a USB device by VID/PID using SetupAPI.

    Args:
        vid: USB Vendor ID
        pid: USB Product ID
        guid_str: Device interface GUID string. Tries mtkclient GUID first,
                  then falls back to standard USB device GUID.

    Returns:
        Device path string or None if not found.
    """
    _load_dlls()
    if _setupapi is None:
        return None

    # Try the mtkclient-specific GUID first, then standard USB GUID
    guids_to_try = []
    if guid_str:
        guids_to_try.append(guid_str)
    guids_to_try.extend([MTKCLIENT_DEVICE_GUID, GUID_DEVINTERFACE_USB_DEVICE])

    vid_pid_str = f"VID_{vid:04X}&PID_{pid:04X}".upper()

    for guid_s in guids_to_try:
        guid = parse_guid(guid_s)
        dev_info = _setupapi.SetupDiGetClassDevsW(
            byref(guid), None, None,
            DIGCF_PRESENT | DIGCF_DEVICEINTERFACE
        )
        if dev_info == INVALID_HANDLE_VALUE:
            continue

        try:
            iface_data = SP_DEVICE_INTERFACE_DATA()
            iface_data.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA)

            idx = 0
            while _setupapi.SetupDiEnumDeviceInterfaces(
                dev_info, None, byref(guid), idx, byref(iface_data)
            ):
                idx += 1

                # Get required buffer size
                required_size = c_ulong(0)
                _setupapi.SetupDiGetDeviceInterfaceDetailW(
                    dev_info, byref(iface_data), None, 0, byref(required_size), None
                )

                # Allocate buffer for detail data
                buf_size = required_size.value
                buf = ctypes.create_string_buffer(buf_size)
                detail = ctypes.cast(buf, POINTER(SP_DEVICE_INTERFACE_DETAIL_DATA_W))
                # cbSize must be set to the size of the fixed portion (on x64: 8)
                ctypes.memmove(buf, ctypes.c_ulong(8 if sizeof(c_void_p) == 8 else 6), 4)

                dev_info_data = SP_DEVINFO_DATA()
                dev_info_data.cbSize = sizeof(SP_DEVINFO_DATA)

                if _setupapi.SetupDiGetDeviceInterfaceDetailW(
                    dev_info, byref(iface_data), buf, buf_size,
                    byref(required_size), byref(dev_info_data)
                ):
                    # Extract device path string (starts at offset 4 in the buffer)
                    path_offset = 4
                    path = ctypes.wstring_at(ctypes.addressof(buf) + path_offset)

                    if vid_pid_str.lower() in path.lower():
                        logger.debug(f"Found device path: {path}")
                        return path
        finally:
            _setupapi.SetupDiDestroyDeviceInfoList(dev_info)

    return None


def find_all_mtk_devices():
    """Find all connected MediaTek USB devices.

    Returns:
        List of dicts with keys: vid, pid, device_path, description
    """
    _load_dlls()
    if _setupapi is None:
        return []

    devices = []
    mtk_vids = [0x0E8D, 0x1004, 0x22D9, 0x0FCE]

    for guid_s in [MTKCLIENT_DEVICE_GUID, GUID_DEVINTERFACE_USB_DEVICE]:
        guid = parse_guid(guid_s)
        dev_info = _setupapi.SetupDiGetClassDevsW(
            byref(guid), None, None,
            DIGCF_PRESENT | DIGCF_DEVICEINTERFACE
        )
        if dev_info == INVALID_HANDLE_VALUE:
            continue

        try:
            dev_info_data = SP_DEVINFO_DATA()
            dev_info_data.cbSize = sizeof(SP_DEVINFO_DATA)
            idx = 0

            while _setupapi.SetupDiEnumDeviceInfo(dev_info, idx, byref(dev_info_data)):
                idx += 1

                # Get instance ID
                instance_id = ctypes.create_unicode_buffer(512)
                if not _setupapi.SetupDiGetDeviceInstanceIdW(
                    dev_info, byref(dev_info_data), instance_id, 512, None
                ):
                    continue

                iid = instance_id.value.upper()

                # Parse VID/PID
                vid = pid = 0
                try:
                    vid_pos = iid.find("VID_")
                    pid_pos = iid.find("PID_")
                    if vid_pos >= 0 and pid_pos >= 0:
                        vid = int(iid[vid_pos + 4:vid_pos + 8], 16)
                        pid = int(iid[pid_pos + 4:pid_pos + 8], 16)
                except (ValueError, IndexError):
                    continue

                if vid not in mtk_vids:
                    continue

                # Get device description
                desc_buf = ctypes.create_unicode_buffer(256)
                _setupapi.SetupDiGetDeviceRegistryPropertyW(
                    dev_info, byref(dev_info_data), SPDRP_DEVICEDESC,
                    None, ctypes.cast(desc_buf, ctypes.POINTER(ctypes.c_ubyte)),
                    512, None
                )

                # Find device path for this device
                device_path = _find_device_path(vid, pid)

                dev = {
                    'vid': vid,
                    'pid': pid,
                    'device_path': device_path or iid,
                    'description': desc_buf.value or "",
                    'instance_id': iid,
                }

                # Avoid duplicates
                if not any(d['vid'] == vid and d['pid'] == pid for d in devices):
                    devices.append(dev)
        finally:
            _setupapi.SetupDiDestroyDeviceInfoList(dev_info)

    return devices


# ── WinUSB device handle ──────────────────────────────────────


class WinUsbDevice:
    """Represents an opened WinUSB device with bulk read/write capability.

    Usage:
        dev = WinUsbDevice()
        if dev.open(0x0E8D, 0x2000):
            dev.write(b'\\x00\\x01\\x02\\x03')
            data = dev.read(64)
            dev.close()
    """

    def __init__(self):
        self._file_handle = None
        self._winusb_handle = None
        self.vid = 0
        self.pid = 0
        self.ep_in = 0
        self.ep_out = 0
        self.max_packet_in = 512
        self.max_packet_out = 512
        self.interface_num = 0
        self._device_path = None
        self._is_open = False

    @property
    def is_open(self):
        return self._is_open

    def open(self, vid, pid, interface=0):
        """Open a WinUSB device by VID/PID.

        Args:
            vid: USB Vendor ID
            pid: USB Product ID
            interface: USB interface number (default 0)

        Returns:
            True on success, False on failure.
        """
        if not _load_dlls() or _winusb is None:
            logger.debug("WinUSB not available")
            return False

        # Find device path
        device_path = _find_device_path(vid, pid)
        if not device_path:
            logger.debug(f"Device VID={vid:#06x} PID={pid:#06x} not found via SetupAPI")
            return False

        return self.open_by_path(device_path, interface)

    def open_by_path(self, device_path, interface=0):
        """Open a WinUSB device by device path.

        Args:
            device_path: Windows device path string
            interface: USB interface number

        Returns:
            True on success, False on failure.
        """
        if not _load_dlls() or _winusb is None:
            return False

        self.close()

        try:
            # Open device file handle
            self._file_handle = _kernel32.CreateFileW(
                device_path,
                GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                None,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
                None
            )

            if self._file_handle is None or self._file_handle == INVALID_HANDLE_VALUE:
                err = _kernel32.GetLastError()
                logger.debug(f"CreateFile failed: error {err}")
                self._file_handle = None
                return False

            # Initialize WinUSB
            winusb_handle = c_void_p()
            if not _winusb.WinUsb_Initialize(self._file_handle, byref(winusb_handle)):
                err = _kernel32.GetLastError()
                logger.debug(f"WinUsb_Initialize failed: error {err}")
                _kernel32.CloseHandle(self._file_handle)
                self._file_handle = None
                return False

            self._winusb_handle = winusb_handle
            self._device_path = device_path
            self.interface_num = interface

            # If requesting a non-zero interface, get associated interface
            if interface > 0:
                assoc_handle = c_void_p()
                if _winusb.WinUsb_GetAssociatedInterface(
                    self._winusb_handle, interface - 1, byref(assoc_handle)
                ):
                    # Use the associated interface handle for I/O
                    _winusb.WinUsb_Free(self._winusb_handle)
                    self._winusb_handle = assoc_handle
                else:
                    logger.debug(f"GetAssociatedInterface({interface}) failed, using default")

            # Query interface and endpoint information
            self._query_endpoints()

            # Set pipe policies for reliable operation
            self._configure_pipes()

            self._is_open = True
            logger.info(
                f"WinUSB device opened: path={device_path}, "
                f"EP_IN=0x{self.ep_in:02X} ({self.max_packet_in}B), "
                f"EP_OUT=0x{self.ep_out:02X} ({self.max_packet_out}B)"
            )
            return True

        except Exception as e:
            logger.error(f"Failed to open WinUSB device: {e}")
            self.close()
            return False

    def _query_endpoints(self):
        """Query USB interface descriptor and endpoint information."""
        if not self._winusb_handle:
            return

        # Get interface descriptor
        iface_desc = USB_INTERFACE_DESCRIPTOR()
        if _winusb.WinUsb_QueryInterfaceSettings(
            self._winusb_handle, 0, byref(iface_desc)
        ):
            logger.debug(
                f"Interface {iface_desc.bInterfaceNumber}: "
                f"class=0x{iface_desc.bInterfaceClass:02X}, "
                f"endpoints={iface_desc.bNumEndpoints}"
            )

            # Query each endpoint (pipe)
            for i in range(iface_desc.bNumEndpoints):
                pipe_info = WINUSB_PIPE_INFORMATION()
                if _winusb.WinUsb_QueryPipe(
                    self._winusb_handle, 0, i, byref(pipe_info)
                ):
                    if pipe_info.PipeId & USB_ENDPOINT_DIRECTION_IN:
                        self.ep_in = pipe_info.PipeId
                        self.max_packet_in = pipe_info.MaximumPacketSize
                        logger.debug(
                            f"  EP IN: 0x{pipe_info.PipeId:02X} "
                            f"maxPacket={pipe_info.MaximumPacketSize}"
                        )
                    else:
                        self.ep_out = pipe_info.PipeId
                        self.max_packet_out = pipe_info.MaximumPacketSize
                        logger.debug(
                            f"  EP OUT: 0x{pipe_info.PipeId:02X} "
                            f"maxPacket={pipe_info.MaximumPacketSize}"
                        )

    def _configure_pipes(self):
        """Set WinUSB pipe policies for reliable operation."""
        if not self._winusb_handle:
            return

        # Set transfer timeout (5 seconds)
        timeout = c_ulong(5000)
        if self.ep_in:
            _winusb.WinUsb_SetPipePolicy(
                self._winusb_handle, self.ep_in,
                PIPE_TRANSFER_TIMEOUT, sizeof(timeout), byref(timeout)
            )
        if self.ep_out:
            _winusb.WinUsb_SetPipePolicy(
                self._winusb_handle, self.ep_out,
                PIPE_TRANSFER_TIMEOUT, sizeof(timeout), byref(timeout)
            )

        # Enable auto-clear stall
        auto_clear = c_ubyte(1)
        if self.ep_in:
            _winusb.WinUsb_SetPipePolicy(
                self._winusb_handle, self.ep_in,
                AUTO_CLEAR_STALL, sizeof(auto_clear), byref(auto_clear)
            )
        if self.ep_out:
            _winusb.WinUsb_SetPipePolicy(
                self._winusb_handle, self.ep_out,
                AUTO_CLEAR_STALL, sizeof(auto_clear), byref(auto_clear)
            )

    def close(self):
        """Close the WinUSB device."""
        if self._winusb_handle:
            try:
                _winusb.WinUsb_Free(self._winusb_handle)
            except Exception:
                pass
            self._winusb_handle = None

        if self._file_handle:
            try:
                _kernel32.CloseHandle(self._file_handle)
            except Exception:
                pass
            self._file_handle = None

        self._is_open = False
        self._device_path = None

    def write(self, data, endpoint=None):
        """Write data to the device via bulk OUT endpoint.

        Args:
            data: bytes to write
            endpoint: endpoint address (default: self.ep_out)

        Returns:
            Number of bytes written, or -1 on error.
        """
        if not self._is_open or not self._winusb_handle:
            return -1

        if endpoint is None:
            endpoint = self.ep_out

        if isinstance(data, (bytearray, memoryview)):
            data = bytes(data)

        buf = ctypes.create_string_buffer(data)
        transferred = c_ulong(0)

        if _winusb.WinUsb_WritePipe(
            self._winusb_handle, endpoint,
            buf, len(data), byref(transferred), None
        ):
            return transferred.value
        else:
            err = _kernel32.GetLastError()
            logger.debug(f"WinUsb_WritePipe failed: error {err}")
            return -1

    def read(self, length, endpoint=None, timeout=None):
        """Read data from the device via bulk IN endpoint.

        Args:
            length: maximum number of bytes to read
            endpoint: endpoint address (default: self.ep_in)
            timeout: read timeout in ms (default: uses pipe policy)

        Returns:
            bytes read, or empty bytes on error/timeout.
        """
        if not self._is_open or not self._winusb_handle:
            return b""

        if endpoint is None:
            endpoint = self.ep_in

        # Set timeout if specified
        if timeout is not None:
            to = c_ulong(timeout)
            _winusb.WinUsb_SetPipePolicy(
                self._winusb_handle, endpoint,
                PIPE_TRANSFER_TIMEOUT, sizeof(to), byref(to)
            )

        buf = ctypes.create_string_buffer(length)
        transferred = c_ulong(0)

        if _winusb.WinUsb_ReadPipe(
            self._winusb_handle, endpoint,
            buf, length, byref(transferred), None
        ):
            return buf.raw[:transferred.value]
        else:
            err = _kernel32.GetLastError()
            if err == 121:  # ERROR_SEM_TIMEOUT
                logger.debug("WinUsb_ReadPipe: timeout")
            else:
                logger.debug(f"WinUsb_ReadPipe failed: error {err}")
            return b""

    def control_transfer(self, request_type, request, value=0, index=0, data_or_length=None):
        """Perform a USB control transfer.

        Args:
            request_type: bmRequestType
            request: bRequest
            value: wValue
            index: wIndex
            data_or_length: bytes to send (OUT) or max length to receive (IN)

        Returns:
            For IN transfers: bytes received
            For OUT transfers: number of bytes sent
            On error: -1
        """
        if not self._is_open or not self._winusb_handle:
            return -1

        setup = WINUSB_SETUP_PACKET()
        setup.RequestType = request_type
        setup.Request = request
        setup.Value = value
        setup.Index = index

        transferred = c_ulong(0)
        is_in = bool(request_type & 0x80)

        if is_in:
            # IN transfer - receive data
            length = data_or_length if isinstance(data_or_length, int) else 0
            setup.Length = length
            buf = ctypes.create_string_buffer(length) if length > 0 else None

            if _winusb.WinUsb_ControlTransfer(
                self._winusb_handle, setup,
                buf, length, byref(transferred), None
            ):
                return buf.raw[:transferred.value] if buf else b""
        else:
            # OUT transfer - send data
            if data_or_length is not None and not isinstance(data_or_length, int):
                data = bytes(data_or_length)
                setup.Length = len(data)
                buf = ctypes.create_string_buffer(data)

                if _winusb.WinUsb_ControlTransfer(
                    self._winusb_handle, setup,
                    buf, len(data), byref(transferred), None
                ):
                    return transferred.value
            else:
                setup.Length = 0
                if _winusb.WinUsb_ControlTransfer(
                    self._winusb_handle, setup,
                    None, 0, byref(transferred), None
                ):
                    return 0

        err = _kernel32.GetLastError()
        logger.debug(f"Control transfer failed: error {err}")
        return -1

    def reset_pipe(self, endpoint=None):
        """Reset a stalled pipe (endpoint).

        Args:
            endpoint: endpoint address (default: resets both IN and OUT)

        Returns:
            True on success.
        """
        if not self._is_open or not self._winusb_handle:
            return False

        success = True
        if endpoint is not None:
            if not _winusb.WinUsb_ResetPipe(self._winusb_handle, endpoint):
                success = False
        else:
            if self.ep_in:
                if not _winusb.WinUsb_ResetPipe(self._winusb_handle, self.ep_in):
                    success = False
            if self.ep_out:
                if not _winusb.WinUsb_ResetPipe(self._winusb_handle, self.ep_out):
                    success = False
        return success

    def flush_pipe(self, endpoint=None):
        """Flush a pipe's pending data.

        Args:
            endpoint: endpoint address (default: flushes IN pipe)
        """
        if not self._is_open or not self._winusb_handle:
            return False

        ep = endpoint or self.ep_in
        if ep:
            return bool(_winusb.WinUsb_FlushPipe(self._winusb_handle, ep))
        return False

    def abort_pipe(self, endpoint=None):
        """Abort pending transfers on a pipe.

        Args:
            endpoint: endpoint address (default: aborts both)
        """
        if not self._is_open or not self._winusb_handle:
            return False

        success = True
        if endpoint is not None:
            if not _winusb.WinUsb_AbortPipe(self._winusb_handle, endpoint):
                success = False
        else:
            if self.ep_in:
                _winusb.WinUsb_AbortPipe(self._winusb_handle, self.ep_in)
            if self.ep_out:
                _winusb.WinUsb_AbortPipe(self._winusb_handle, self.ep_out)
        return success

    def set_timeout(self, timeout_ms, endpoint=None):
        """Set the transfer timeout for an endpoint.

        Args:
            timeout_ms: timeout in milliseconds
            endpoint: endpoint address (default: sets both)
        """
        if not self._is_open or not self._winusb_handle:
            return

        timeout = c_ulong(timeout_ms)
        if endpoint is not None:
            _winusb.WinUsb_SetPipePolicy(
                self._winusb_handle, endpoint,
                PIPE_TRANSFER_TIMEOUT, sizeof(timeout), byref(timeout)
            )
        else:
            if self.ep_in:
                _winusb.WinUsb_SetPipePolicy(
                    self._winusb_handle, self.ep_in,
                    PIPE_TRANSFER_TIMEOUT, sizeof(timeout), byref(timeout)
                )
            if self.ep_out:
                _winusb.WinUsb_SetPipePolicy(
                    self._winusb_handle, self.ep_out,
                    PIPE_TRANSFER_TIMEOUT, sizeof(timeout), byref(timeout)
                )


# ── Device management (SetupAPI / CfgMgr32) ──────────────────


def reset_usb_device(vid, pid):
    """Reset a USB device without physical disconnect using CfgMgr32.

    Args:
        vid: USB Vendor ID
        pid: USB Product ID

    Returns:
        True on success, False on failure.
    """
    _load_dlls()
    if _setupapi is None or _cfgmgr32 is None:
        return False

    guid = parse_guid(GUID_DEVINTERFACE_USB_DEVICE)
    dev_info = _setupapi.SetupDiGetClassDevsW(
        byref(guid), None, None,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE
    )
    if dev_info == INVALID_HANDLE_VALUE:
        return False

    vid_pid = f"VID_{vid:04X}&PID_{pid:04X}".upper()
    result = False

    try:
        dev_info_data = SP_DEVINFO_DATA()
        dev_info_data.cbSize = sizeof(SP_DEVINFO_DATA)
        idx = 0

        while _setupapi.SetupDiEnumDeviceInfo(dev_info, idx, byref(dev_info_data)):
            idx += 1

            instance_id = ctypes.create_unicode_buffer(512)
            if not _setupapi.SetupDiGetDeviceInstanceIdW(
                dev_info, byref(dev_info_data), instance_id, 512, None
            ):
                continue

            if vid_pid not in instance_id.value.upper():
                continue

            # Get parent hub device instance
            parent_inst = c_ulong()
            cr = _cfgmgr32.CM_Get_Parent(byref(parent_inst), dev_info_data.DevInst, 0)
            if cr == 0:  # CR_SUCCESS
                cr = _cfgmgr32.CM_Reenumerate_DevNode(
                    parent_inst, CM_REENUMERATE_RETRY_INSTALLATION
                )
                if cr == 0:
                    logger.info(f"Device VID={vid:#06x} PID={pid:#06x} re-enumerated")
                    time.sleep(1.0)
                    result = True
                    break

            # Fallback: disable/enable device
            pcp = SP_PROPCHANGE_PARAMS()
            pcp.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER)
            pcp.ClassInstallHeader.InstallFunction = DIF_PROPERTYCHANGE
            pcp.StateChange = DICS_DISABLE
            pcp.Scope = DICS_FLAG_CONFIGSPECIFIC
            pcp.HwProfile = 0

            if _setupapi.SetupDiSetClassInstallParamsW(
                dev_info, byref(dev_info_data),
                byref(pcp.ClassInstallHeader), sizeof(pcp)
            ):
                _setupapi.SetupDiCallClassInstaller(
                    DIF_PROPERTYCHANGE, dev_info, byref(dev_info_data)
                )
                time.sleep(0.5)

                pcp.StateChange = DICS_ENABLE
                if _setupapi.SetupDiSetClassInstallParamsW(
                    dev_info, byref(dev_info_data),
                    byref(pcp.ClassInstallHeader), sizeof(pcp)
                ):
                    if _setupapi.SetupDiCallClassInstaller(
                        DIF_PROPERTYCHANGE, dev_info, byref(dev_info_data)
                    ):
                        time.sleep(1.0)
                        result = True
                        break
    finally:
        _setupapi.SetupDiDestroyDeviceInfoList(dev_info)

    return result


def disable_selective_suspend(vid, pid):
    """Disable USB selective suspend for a device.

    Prevents Windows from power-managing the device during operations.

    Args:
        vid: USB Vendor ID
        pid: USB Product ID

    Returns:
        True on success, False on failure.
    """
    key_path = f"SYSTEM\\CurrentControlSet\\Enum\\USB\\VID_{vid:04X}&PID_{pid:04X}"
    try:
        usb_key = winreg.OpenKey(
            winreg.HKEY_LOCAL_MACHINE, key_path,
            access=winreg.KEY_READ | winreg.KEY_ENUMERATE_SUB_KEYS
        )
    except OSError:
        return False

    result = False
    try:
        idx = 0
        while True:
            try:
                subkey_name = winreg.EnumKey(usb_key, idx)
                idx += 1

                params_path = f"{key_path}\\{subkey_name}\\Device Parameters"
                try:
                    params_key = winreg.OpenKey(
                        winreg.HKEY_LOCAL_MACHINE, params_path,
                        access=winreg.KEY_SET_VALUE
                    )
                    winreg.SetValueEx(
                        params_key, "SelectiveSuspendEnabled",
                        0, winreg.REG_DWORD, 0
                    )
                    winreg.CloseKey(params_key)
                    result = True
                except OSError:
                    pass
            except OSError:
                break
    finally:
        winreg.CloseKey(usb_key)

    return result


def check_winusb_driver(vid, pid):
    """Check if WinUSB driver is installed for a specific device.

    Returns:
        True if WinUSB driver is bound to the device.
    """
    _load_dlls()
    if _setupapi is None:
        return False

    guid = parse_guid(GUID_DEVINTERFACE_USB_DEVICE)
    dev_info = _setupapi.SetupDiGetClassDevsW(
        byref(guid), None, None,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE
    )
    if dev_info == INVALID_HANDLE_VALUE:
        return False

    vid_pid = f"VID_{vid:04X}&PID_{pid:04X}".upper()
    result = False

    try:
        dev_info_data = SP_DEVINFO_DATA()
        dev_info_data.cbSize = sizeof(SP_DEVINFO_DATA)
        idx = 0

        while _setupapi.SetupDiEnumDeviceInfo(dev_info, idx, byref(dev_info_data)):
            idx += 1

            instance_id = ctypes.create_unicode_buffer(512)
            if not _setupapi.SetupDiGetDeviceInstanceIdW(
                dev_info, byref(dev_info_data), instance_id, 512, None
            ):
                continue

            if vid_pid not in instance_id.value.upper():
                continue

            # Get driver service name
            driver_buf = ctypes.create_unicode_buffer(256)
            if _setupapi.SetupDiGetDeviceRegistryPropertyW(
                dev_info, byref(dev_info_data), SPDRP_SERVICE,
                None, ctypes.cast(driver_buf, ctypes.POINTER(ctypes.c_ubyte)),
                512, None
            ):
                driver = driver_buf.value.lower()
                if driver in ('winusb', 'libusb0', 'libusbk'):
                    result = True
                    break
    finally:
        _setupapi.SetupDiDestroyDeviceInfoList(dev_info)

    return result
