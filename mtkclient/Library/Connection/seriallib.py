#!/usr/bin/python3
# -*- coding: utf-8 -*-
# (c) B.Kerler 2018-2025
import time
import sys
import logging
from queue import Queue

from mtkclient.Library.DA.xmlflash.xml_param import max_xml_data_length
import serial
import serial.tools.list_ports
import inspect
from mtkclient.Library.Connection.devicehandler import DeviceClass

if sys.platform != "win32":
    import termios


def _reset_input_buffer():
    return


def _reset_input_buffer_org(self):
    if sys.platform != "win32":
        return termios.tcflush(self.fd, termios.TCIFLUSH)


class SerialClass(DeviceClass):

    def __init__(self, loglevel=logging.INFO, portconfig=None, devclass=-1):
        super().__init__(loglevel, portconfig, devclass)
        self.is_serial = True
        self.device = None
        self.queue = Queue()
        # vid/pid are populated by detectdevices() for the first matched port;
        # Port.py may read self.cdc.pid to distinguish BROM (0x0003) from
        # Preloader/DA.  On COM port paths this is 0 unless detectdevices()
        # finds an exact VID/PID match.
        self.vid = 0
        self.pid = 0

    def connect(self, ep_in=-1, ep_out=-1):
        if self.connected:
            self.close()
            self.connected = False

        ports = self.detectdevices()
        if ports:
            if self.portname != "DETECT":
                if self.portname not in ports:
                    # The device re-enumerated (e.g. Preloader→DA mode switch)
                    # and may have been assigned a different COM port number.
                    # Fall back to auto-detection instead of failing hard.
                    self.info(
                        "{} not in detected ports {}, falling back to "
                        "auto-detect".format(self.portname, ports))
                    port = ports[0]
                else:
                    port = ports[ports.index(self.portname)]
            else:
                port = ports[0]
            self.debug("Got port: {}, initializing".format(port))
            # Construct Serial with port=None so pyserial does NOT open (and
            # therefore does NOT call reset_input_buffer) during __init__.
            # We patch _reset_input_buffer to a no-op first, then open, so
            # any bytes that arrived before we opened (pre-queued by the OS)
            # are preserved for the handshake.
            self.device = serial.Serial(port=None, baudrate=115200, bytesize=serial.EIGHTBITS,
                                        parity=serial.PARITY_NONE, stopbits=serial.STOPBITS_ONE,
                                        timeout=0.5,
                                        xonxoff=False, dsrdtr=False, rtscts=False)
            self.device.port = port
            self.portname = port
        else:
            return False
        self.device._reset_input_buffer = _reset_input_buffer
        try:
            self.device.open()
        except Exception as e:
            self.debug(e)
            pass
        self.device._reset_input_buffer = _reset_input_buffer_org
        self.connected = self.device.is_open
        if self.connected:
            return True
        return False

    def setportname(self, portname: str):
        self.portname = portname

    def set_fast_mode(self, enabled):
        """Switch between normal and fast (high-throughput) read modes.

        In fast mode (after DA stage 2 connects at USB high-speed):
          - Raise the pyserial read timeout so large reads don't time out
            mid-transfer.
          - Raise the in-buffer to absorb burst writes from the DA.

        In normal mode (Preloader / BROM communication at 115200 baud):
          - Restore the conservative 500 ms timeout used during handshake.
        """
        if self.device is None:
            return
        if enabled:
            # High-throughput mode: long timeout, generous inter-char gap
            self.device.timeout  = 3.0      # 3 s total read deadline
            self.device.inter_byte_timeout = None
        else:
            # Conservative mode: 500 ms timeout for command/response cycles
            self.device.timeout  = 0.5
            self.device.inter_byte_timeout = None

    def change_baud(self):
        print("Changing Baudrate")
        self.write(b'\xD2' + b'\x02' + b'\x01')
        self.read(1)
        self.write(b'\x5a')
        # self.read(1)
        self.device.baudrate = 460800
        time.sleep(0.2)
        for i in range(10):
            self.write(b'\xc0')
            self.read(1)
            time.sleep(0.02)
        self.write(b'\x5a')
        self.read(1)

    def close(self, reset=False):
        if self.connected:
            self.device.close()
            del self.device
            self.device = None
            self.connected = False
        if reset:
            # reset=True signals a mode-switch (e.g. Preloader → DA).
            # The device will re-enumerate with a different PID and may
            # receive a different COM port number, so clear the cached port
            # name so that the next connect() call uses auto-detection.
            self.portname = "DETECT"

    def detectdevices(self):
        ids = []
        # Build a normalised VID→{PID→iface} lookup regardless of whether
        # portconfig was passed as a dict {vid:{pid:iface}} or as a list of
        # triples [[vid, pid, iface], ...].
        if isinstance(self.portconfig, dict):
            vid_pid_map = self.portconfig
        else:
            vid_pid_map = {}
            for entry in self.portconfig:
                if isinstance(entry, (list, tuple)) and len(entry) >= 2:
                    vid, pid = int(entry[0]), int(entry[1])
                    iface = entry[2] if len(entry) > 2 else -1
                    vid_pid_map.setdefault(vid, {})[pid] = iface

        for port in serial.tools.list_ports.comports():
            if "ttyUSB" in port.device or "ttyACM" in port.device:
                if port.device not in ids:
                    ids.append(port.device)
            elif port.vid is not None and port.pid is not None:
                # Windows/macOS: match against known VID/PID table
                if port.vid in vid_pid_map and port.pid in vid_pid_map[port.vid]:
                    self.info(f"Detected {hex(port.vid)}:{hex(port.pid)} device at: {port.device}")
                    if port.device not in ids:
                        ids.append(port.device)
                        # Track VID/PID of the first matched device so that
                        # callers (e.g. Port.run_handshake) can query them.
                        if self.vid == 0:
                            self.vid = port.vid
                            self.pid = port.pid
        return sorted(ids)

    def set_line_coding(self, baudrate=None, parity=0, databits=8, stopbits=1):
        """Apply line coding (baud, parity, data bits, stop bits) to the
        pyserial port.

        mtkclient passes integer parity codes matching CDC ACM convention:
            0 = None, 1 = Odd, 2 = Even, 3 = Mark, 4 = Space, None = None
        pyserial requires single-character strings for parity.
        """
        import serial as _serial
        _parity_map = {
            None: _serial.PARITY_NONE,
            0:    _serial.PARITY_NONE,
            1:    _serial.PARITY_ODD,
            2:    _serial.PARITY_EVEN,
            3:    _serial.PARITY_MARK,
            4:    _serial.PARITY_SPACE,
        }
        _stop_map = {
            None: _serial.STOPBITS_ONE,
            0:    _serial.STOPBITS_ONE,          # CDC format 0 = 1 stop bit
            1:    _serial.STOPBITS_ONE,
            1.5:  _serial.STOPBITS_ONE_POINT_FIVE,
            2:    _serial.STOPBITS_TWO,
        }
        if baudrate is not None and baudrate > 0:
            self.device.baudrate = baudrate
        if parity not in _parity_map:
            self.debug("set_line_coding: unknown parity code {!r}, defaulting to NONE".format(parity))
        if stopbits not in _stop_map:
            self.debug("set_line_coding: unknown stopbits value {!r}, defaulting to 1".format(stopbits))
        if databits not in (5, 6, 7, 8):
            self.debug("set_line_coding: invalid databits {!r}, defaulting to 8".format(databits))
        self.device.parity   = _parity_map.get(parity,  _serial.PARITY_NONE)
        self.device.stopbits = _stop_map.get(stopbits,  _serial.STOPBITS_ONE)
        self.device.bytesize = databits if databits in (5, 6, 7, 8) else 8
        self.debug("Linecoding set: baud={} parity={} data={} stop={}".format(
            self.device.baudrate, self.device.parity,
            self.device.bytesize, self.device.stopbits))

    def setbreak(self):
        self.device.send_break()
        self.debug("Break set")

    def setcontrollinestate(self, rts=None, dtr=None, is_ftdi=False):
        self.device.rts = rts
        self.device.dtr = dtr
        self.debug("Linecoding set")

    def write(self, command, pktsize=None):
        if pktsize is None:
            pktsize = 512
        if isinstance(command, str):
            command = bytes(command, 'utf-8')
        pos = 0
        if command == b'':
            try:
                self.device.write(b'')
            except Exception as err:
                error = str(err)
                if "timeout" in error:
                    # time.sleep(0.01)
                    try:
                        self.device.write(b'')
                    except Exception as err:
                        self.debug(str(err))
                        return False
                return True
        else:
            i = 0
            while pos < len(command):
                try:
                    ctr = self.device.write(command[pos:pos + pktsize])
                    if ctr is None or ctr <= 0:
                        # Write returned nothing written — treat as error,
                        # retry up to 3 times then give up.
                        self.debug("write() returned {}, retrying".format(ctr))
                        i += 1
                        if i >= 3:
                            self.connected = False
                            return False
                    else:
                        pos += ctr
                        i = 0      # reset error counter on success
                except Exception as err:
                    self.debug(str(err))
                    # print("Error while writing")
                    # time.sleep(0.01)
                    i += 1
                    if i == 3:
                        # Mark as disconnected so serial_handshake() reconnects
                        # instead of looping forever on a dead port.
                        self.connected = False
                        return False
                    pass
        self.verify_data(bytearray(command), "TX:")
        # Do NOT call reset_output_buffer() here: on Windows that maps to
        # PurgeComm(PURGE_TXABORT|PURGE_TXCLEAR) which discards bytes that
        # have been queued but not yet transmitted — causing the device to
        # never receive the data.  usbwrite() calls flush() after us, which
        # waits for the bytes to be sent.  For direct write() callers
        # (e.g. run_serial_handshake), the 5 ms sleep below is enough time
        # for a single byte at any baud rate to leave the OS TX buffer.
        time.sleep(0.005)
        """
        while self.device.in_waiting == 0:
            time.sleep(0.005)
            timeout+=1
            if timeout==10:
                break
        """
        return True

    def read(self, length=None, timeout=-1):
        if timeout == -1:
            timeout = self.timeout
        if length is None:
            length = self.device.in_waiting
            if length == 0:
                return b""
        if self.xmlread:
            if length > self.device.in_waiting:
                length = self.device.in_waiting
        return self.usbread(resplen=length, maxtimeout=timeout)

    def get_device(self):
        return self.device

    def get_read_packetsize(self):
        return 0x200

    def get_write_packetsize(self):
        return 0x200

    def flush(self):
        if self.device is not None:
            try:
                self.device.reset_output_buffer()
            except Exception:
                pass
            try:
                return self.device.flush()
            except Exception:
                pass
        return None

    def get_interface_count(self):
        # A COM port presents as a single serial interface.
        return 1
    def usbread(self, resplen=None, maxtimeout=0, timeout=0, w_max_packet_size=None):
        # print("Reading {} bytes".format(resplen))
        if timeout == 0 and maxtimeout != 0:
            timeout = maxtimeout / 1000  # Some code calls this with ms delays, some with seconds.
        if timeout < 0.02:
            timeout = 0.02
        if resplen is None:
            if self.device is None:
                return b""
            try:
                resplen = self.device.in_waiting
            except Exception as e:
                self.debug("usbread: could not read in_waiting ({}); device disconnected".format(e))
                self.connected = False
                return b""
        # if resplen <= 0:
        #    self.info("Warning !")
        res = bytearray()
        loglevel = self.loglevel
        if self.device is None:
            return b""
        try:
            self.device.timeout = timeout
        except Exception as e:
            # Device disconnected: pyserial's SetCommTimeouts (Windows) or
            # equivalent raises SerialException("Cannot configure port…") when
            # the underlying COM handle is no longer valid.
            self.debug("usbread: could not set timeout ({}); device disconnected".format(e))
            self.connected = False
            return b""
        epr = self.device.read
        q = self.queue
        extend = res.extend
        bytestoread = resplen
        while bytestoread:
            bytestoread = resplen - len(res) if len(res) < resplen else 0
            if not q.empty():
                data = q.get(bytestoread)
                extend(data)
            if bytestoread <= 0:
                break
            try:
                val = epr(bytestoread)
                if len(val) == 0:
                    break
                if len(val) > bytestoread:
                    self.warning("Buffer overflow")
                    q.put(val[bytestoread:])
                    extend(val[:bytestoread])
                else:
                    extend(val)
            except Exception as e:
                error = str(e)
                if "timed out" in error:
                    if timeout is None:
                        return b""
                    self.debug("Timed out")
                    if timeout == 10:
                        return b""
                    timeout += 1
                    pass
                elif "Overflow" in error:
                    self.error("USB Overflow")
                    return b""
                else:
                    self.info(repr(e))
                    return b""

        if loglevel == logging.DEBUG:
            self.debug("SERIAL " + inspect.currentframe().f_back.f_code.co_name + ": length(" + hex(resplen) + ")")
            if self.loglevel == logging.DEBUG:
                self.verify_data(res[:resplen], "RX:")
        return res[:resplen]

    def usbxmlread(self, timeout=0):
        resplen = self.device.in_waiting
        res = bytearray()
        loglevel = self.loglevel
        self.device.timeout = timeout
        epr = self.device.read
        extend = res.extend
        bytestoread = max_xml_data_length
        while len(res) < bytestoread:
            try:
                val = epr(bytestoread)
                if len(val) == 0:
                    break
                extend(val)
                # res is bytearray; res[-1] returns int — compare to 0, not b"\x00"
                if res[-1] == 0:
                    break
            except Exception as e:
                error = str(e)
                if "timed out" in error:
                    if timeout is None:
                        return b""
                    self.debug("Timed out")
                    if timeout == 10:
                        return b""
                    timeout += 1
                    pass
                elif "Overflow" in error:
                    self.error("USB Overflow")
                    return b""
                else:
                    self.info(repr(e))
                    return b""

        if loglevel == logging.DEBUG:
            self.debug("SERIAL " + inspect.currentframe().f_back.f_code.co_name + ": length(" + hex(resplen) + ")")
            if self.loglevel == logging.DEBUG:
                self.verify_data(res[:resplen], "RX:")
        return res[:resplen]

    def usbwrite(self, data, pktsize=None):
        if pktsize is None:
            pktsize = len(data)
        res = self.write(data, pktsize)
        self.device.flush()
        return res

    def usbreadwrite(self, data, resplen):
        self.usbwrite(data)  # size
        self.device.flush()
        res = self.usbread(resplen)
        return res

    def ctrl_transfer(self, bm_request_type, b_request, w_value=0, w_index=0, data_or_w_length=None):
        """
        Translate USB CDC class-specific control requests to pyserial calls.

        Exploit code (kamakiri, kamakiripl) calls cdc.device.ctrl_transfer()
        directly.  When running on Windows via the KMDF driver the device
        object is a pyserial Serial instance, not a pyusb Device, so we
        intercept those calls here and map the most important CDC ACM
        commands to the equivalent pyserial operations.

        Requests that cannot be meaningfully mapped (e.g. raw descriptor
        fetches used by kamakiri's exploit path) are silently ignored — the
        exploit is inherently a libusb/USB-raw-access operation and does not
        work over a COM port; callers should check is_serial before using it.
        """
        bmRT = bm_request_type & 0xFF
        direction_in = bool(bmRT & 0x80)

        CDC_SET_LINE_CODING = 0x20
        CDC_GET_LINE_CODING = 0x21
        CDC_SET_CONTROL_LINE_STATE = 0x22
        CDC_SEND_BREAK = 0x23

        if b_request == CDC_GET_LINE_CODING and direction_in:
            # Return current line coding (7 bytes: dwDTERate LE32, bCharFormat, bParityType, bDataBits)
            from struct import pack
            br = self.device.baudrate if self.device else 115200
            return bytearray(pack('<IBBB', br, 0, 0, 8))

        if b_request == CDC_SET_LINE_CODING and not direction_in:
            # Apply line coding
            if data_or_w_length is not None and len(data_or_w_length) >= 7:
                from struct import unpack_from
                baud = unpack_from('<I', data_or_w_length, 0)[0]
                stop = data_or_w_length[4]
                parity = data_or_w_length[5]
                databits = data_or_w_length[6]
                if self.device and baud > 0:
                    self.device.baudrate = baud
                    import serial as _serial
                    parity_map = {0: _serial.PARITY_NONE, 1: _serial.PARITY_ODD,
                                  2: _serial.PARITY_EVEN, 3: _serial.PARITY_MARK,
                                  4: _serial.PARITY_SPACE}
                    stop_map = {0: _serial.STOPBITS_ONE, 1: _serial.STOPBITS_ONE_POINT_FIVE,
                                2: _serial.STOPBITS_TWO}
                    self.device.parity = parity_map.get(parity, _serial.PARITY_NONE)
                    self.device.stopbits = stop_map.get(stop, _serial.STOPBITS_ONE)
                    self.device.bytesize = databits if databits in (5, 6, 7, 8) else 8
            return bytearray()

        if b_request == CDC_SET_CONTROL_LINE_STATE and not direction_in:
            # w_value bit0 = DTR, bit1 = RTS
            if self.device:
                self.device.dtr = bool(w_value & 0x01)
                self.device.rts = bool(w_value & 0x02)
            return bytearray()

        if b_request == CDC_SEND_BREAK and not direction_in:
            if self.device:
                self.device.send_break()
            return bytearray()

        # All other requests (descriptor fetches, vendor-specific, etc.) are
        # not meaningful over a COM port — return empty data.
        if direction_in:
            size = data_or_w_length if isinstance(data_or_w_length, int) else 0
            return bytearray(size)
        return bytearray()
