# MTK USB-to-Serial Opensource Driver — Installation Guide

Opensource KMDF CDC ACM driver (`mtk_usb2ser.sys`) that replaces the proprietary
`usb2ser.sys`.  Once installed, MediaTek BROM / Preloader / DA devices appear as
native **COM ports** under Windows.  This eliminates the need for **libusb** or
**UsbDk** — mtkclient talks to the device directly through its serial backend.

---

## Installation Options

### Option A — KMDF driver (`mtk_usb2ser.sys`)  ★ recommended

Full-featured kernel-mode driver built from this source tree.  Provides CDC ACM
serial emulation with idle-power management and WMI support.

*Requires building with Visual Studio + WDK, or using a pre-built `.sys` file.*

### Option B — CDC INF with custom `usb2ser.sys`

Located in `Setup/Windows/driver/CDC/`.  This INF maps all MediaTek USB hardware
IDs (100+ device modes) to the custom `usb2ser.sys` kernel driver — MediaTek's
WDM CDC ACM implementation with enhanced buffer management, reordering, and
idle/selective-suspend support specific to MTK chipsets.

The `usb2ser.sys` binary must be placed alongside the INF (in `x64/` and/or
`x86/` subdirectories).  The INF's `SourceDisksFiles` section specifies
`usb2ser.sys=1` which tells Windows to copy the driver from the installation
media.

> **Note:** On Windows, mtkclient **automatically** detects the COM port created
> by either driver and uses its serial backend — no `--serialport` flag required.

---

## 1 — Quick Install (PowerShell script)

The easiest method.  Open an **Administrator PowerShell** prompt:

```powershell
# Option A (KMDF driver)
.\Setup\Windows\installer\install_driver.ps1

# Option B (CDC-only INF)
pnputil.exe /add-driver "Setup\Windows\driver\CDC\mtk_preloader_opensource.inf" /install
```

The script locates `mtk_usb2ser.inf`, registers it via `pnputil`, and verifies
the driver store entry.

---

## 2 — Manual Install with pnputil

Open an **Administrator** Command Prompt or PowerShell:

```bat
REM Option A
pnputil.exe /add-driver "Setup\Windows\driver\opensource\mtk_usb2ser.inf" /install

REM Option B
pnputil.exe /add-driver "Setup\Windows\driver\CDC\mtk_preloader_opensource.inf" /install
```

Confirm the driver appears in the store:

```bat
pnputil.exe /enum-drivers | findstr /i "mtk_usb2ser"
```

---

## 3 — Device Manager Manual Install

1. Connect the MediaTek device (BROM / Preloader / DA mode).
2. Open **Device Manager** → locate the unknown device under *Other devices*.
3. Right-click → **Update driver** → **Browse my computer for drivers**.
4. Point to `Setup\Windows\driver\opensource\` (Option A) or
   `Setup\Windows\driver\CDC\` (Option B).
5. Accept any unsigned-driver warnings (see *Test Signing* below).
6. The device should move to **Ports (COM & LPT)** with an assigned COM number.

---

## 4 — Test Signing (Option A only)

The KMDF driver is not production-signed.  Windows requires test signing to be
enabled before it will load `mtk_usb2ser.sys`:

```bat
bcdedit /set testsigning on
```

Reboot after running the command.  A "Test Mode" watermark will appear on the
desktop — this is expected.

To disable later:

```bat
bcdedit /set testsigning off
```

> **Option B does not need test signing** because it uses the inbox `usbser.sys`
> which is already Microsoft-signed.

---

## 5 — Verify Installation

1. Plug in a MediaTek device in BROM, Preloader, or DA mode.
2. Open **Device Manager** → expand **Ports (COM & LPT)**.
3. You should see one of:
   - *MediaTek BROM USB Port (0003)*
   - *MediaTek Preloader USB Port (2000)*
   - *MediaTek DA USB Port (2001)*
4. Note the assigned **COM port number** (e.g. `COM3`).

From PowerShell you can also check:

```powershell
Get-CimInstance Win32_PnPEntity | Where-Object { $_.Name -match 'MediaTek' }
```

---

## 6 — Using with mtkclient

With the `usb2ser.sys` or `mtk_usb2ser.sys` driver installed, the MTK device
appears as a standard COM port under **Ports (COM & LPT)**.  On Windows,
mtkclient **automatically** uses its serial backend — no libusb, UsbDk, or
`--serialport` flag required.

```bash
# mtkclient auto-detects the COM port on Windows (no extra flags needed)
python mtk.py printgpt

# Run multiple commands in sequence without disconnecting the device
python mtk.py rl out --skip userdata
python mtk.py reset
```

**How it works:** On Windows, mtkclient detects that the platform is `win32` and
automatically switches to `SerialClass` with auto-detection mode.  It uses
PySerial's `serial.tools.list_ports` to scan for devices with MediaTek VID
`0x0E8D`, or by driver description containing "MediaTek" or "MTK".

If auto-detection fails, specify the port manually:

```bash
python mtk.py printgpt --serialport COM3
```

---

## Supported Hardware IDs

| Mode | VID | PID | Hardware ID | Description |
|------|-----|------|-------------|-------------|
| BROM | 0E8D | 0003 | `USB\VID_0E8D&PID_0003` | BootROM download mode |
| Preloader | 0E8D | 2000 | `USB\VID_0E8D&PID_2000` | Preloader download mode |
| DA | 0E8D | 2001 | `USB\VID_0E8D&PID_2001` | Download Agent mode |
| VCOM | 0E8D | 2006 | `USB\VID_0E8D&PID_2006&MI_02` | Virtual COM (interface 2) |
| VCOM | 0E8D | 2007 | `USB\VID_0E8D&PID_2007` | Virtual COM |

All IDs use MediaTek's USB Vendor ID **0x0E8D**.

---

## Troubleshooting

| Problem | Fix |
|---------|-----|
| Device shows under *Other devices* | Right-click → Update driver, browse to the INF directory. |
| "Windows cannot verify the digital signature" | Enable test signing: `bcdedit /set testsigning on` and reboot. |
| COM port appears then vanishes | Normal during BROM/Preloader handoff — the device re-enumerates with a new PID. mtkclient handles this automatically. |
| `pnputil` reports "Access denied" | Run the command from an **Administrator** prompt. |
| mtkclient does not find the port | Ensure PySerial is installed (`pip install pyserial`). Try `--port COMx` explicitly. |
| Port number keeps changing | Windows assigns COM numbers dynamically. Use Device Manager → port properties → *Port Settings* → *Advanced* to pin a number. |
| Driver loads but no data transfer | Verify the device is in the expected mode (BROM vs Preloader vs DA). Check USB cable — use a data cable, not charge-only. |

---

## Uninstalling

### PowerShell script

```powershell
.\Setup\Windows\installer\uninstall_driver.ps1
```

The script finds the OEM driver-store entry, removes it with `pnputil`, and
cleans up stale `SERIALCOMM` registry values left by the CDC ACM mapping.

### Manual removal

```bat
REM List installed OEM drivers to find the entry
pnputil.exe /enum-drivers | findstr /i "mtk_usb2ser"

REM Delete it (replace oemXX.inf with the actual name)
pnputil.exe /delete-driver oemXX.inf /uninstall /force
```

After uninstalling, reconnect the device so Windows re-evaluates driver
selection.

---

## License

MIT — see the source files for the full copyright notice.
