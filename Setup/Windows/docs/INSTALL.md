# Installation Guide — MTK USB Serial Driver for Windows 11

## Prerequisites

- **Windows 11** (x64) — Windows 10 x64 also supported
- An MTK-based device (phone, tablet, SoC board)
- Administrator privileges

## Driver Signing

The driver is signed with a **project certificate** generated during CI build. The installer and PowerShell script **automatically install this certificate** into your Windows trust stores, so no manual signing configuration is needed.

> **If you use the installer (`MTK_USB_Driver_Setup_1.0.0.exe`) or the PowerShell script (`install_driver.ps1`), the certificate is installed automatically — just run as Administrator.**

For manual installation (e.g., via `pnputil`), install the certificate first:

```cmd
:: Run as Administrator
certutil -addstore Root mtk_usb2ser.cer
certutil -addstore TrustedPublisher mtk_usb2ser.cer
pnputil /add-driver mtk_usb2ser.inf /install
```

## Installation Options

| Option | Driver | Custom Binary | Best For |
|--------|--------|:------------:|----------|
| **A. KMDF Driver** (recommended) | `mtk_usb2ser.sys` | Yes | Full features, WMI, power management |
| **B. CDC-Only INF** | Windows inbox `usbser.sys` | No | Quick setup, no build required |

---

## Option A: Install the KMDF Driver (Recommended)

### Using the Installer

If you downloaded the installer package from [Releases](https://github.com/Uaemextop/mtk-loader-drivers-opensource-win11/releases):

```powershell
# Run as Administrator
.\MTK_USB_Driver_Setup_1.0.0.exe
```

The installer will:
1. Install the signing certificate into Windows trust stores
2. Copy all driver files
3. Register the driver automatically

### Using PowerShell

```powershell
# Run as Administrator
powershell -ExecutionPolicy Bypass -File installer\install_driver.ps1
```

The script automatically installs the signing certificate before the driver.

### Using pnputil (Manual)

```cmd
:: Run as Administrator — install certificate first, then driver
certutil -addstore Root mtk_usb2ser.cer
certutil -addstore TrustedPublisher mtk_usb2ser.cer
pnputil /add-driver driver\opensource\mtk_usb2ser.inf /install
```

### Using Device Manager

1. First install the certificate: `certutil -addstore Root mtk_usb2ser.cer` and `certutil -addstore TrustedPublisher mtk_usb2ser.cer`
2. Connect your MTK device in preloader/BROM mode
3. Open **Device Manager** (Win+X → Device Manager)
4. Find the unknown device (under "Other Devices" with ⚠️ icon)
5. Right-click → **Update driver**
6. Choose **Browse my computer for drivers**
7. Navigate to the `driver\opensource\` folder (or the build output folder)
8. Click **Next** → accept any warnings

---

## Option B: Install the CDC-Only INF (No Build Required)

This option uses the Windows built-in `usbser.sys` driver — **no custom kernel binary, no certificate needed**. Works immediately.

```cmd
:: Run as Administrator
pnputil /add-driver driver\CDC\mtk_preloader_opensource.inf /install
```

Or install via Device Manager by browsing to the `driver\CDC\` folder.

---

## Verify Installation

1. Connect your MTK device
2. Open **Device Manager**
3. Under **Ports (COM & LPT)** you should see:
   - **MediaTek USB Port (BROM)** — for Boot ROM mode (PID 0003)
   - **MediaTek PreLoader USB VCOM Port** — for preloader mode (PID 2000)
   - **MediaTek DA USB VCOM Port** — for download agent mode (PID 2001)
4. Note the COM port number (e.g., COM3)
5. Use this COM port in your flash tool

## Using with Flash Tools

### SP Flash Tool
1. Install the driver using any method above
2. Open SP Flash Tool
3. Load your scatter file
4. The tool will auto-detect the COM port when you connect the device
5. Click **Download** → connect the device in BROM/preloader mode

### mtkclient
```bash
# mtkclient will auto-detect the COM port
python mtk r boot1 boot1.img
python mtk w boot1 boot1_patched.img
```

### SN Write Tool / MAUI META
1. Set the device to Meta mode
2. The Meta VCOM port will appear under COM & LPT
3. Select the COM port in the tool

## Supported Hardware IDs

| PID | Mode | Description |
|-----|------|-------------|
| `0003` | BROM | Boot ROM — primary flash mode |
| `2000` | Preloader | Preloader VCOM |
| `2001` | DA | Download Agent VCOM |
| `2006`–`2007` | Meta | Meta mode VCOM |
| `200A`–`205F` | Various | Composite, ETS, ELT, Modem, AT ports |

## Uninstalling

```powershell
# Run as Administrator
powershell -ExecutionPolicy Bypass -File installer\uninstall_driver.ps1
```

Or manually via Device Manager: right-click the device → **Uninstall device** → check **Delete the driver software**.

## Troubleshooting

### Error 0x800B0109 (Untrusted root certificate)
The signing certificate was not installed before the driver. Solutions:
1. **Recommended:** Use the installer or PowerShell script — they install the certificate automatically
2. Install the certificate manually: `certutil -addstore Root mtk_usb2ser.cer` and `certutil -addstore TrustedPublisher mtk_usb2ser.cer`
3. Or use the CDC-only INF (Option B) which uses Windows' signed inbox driver

### Error Code 52 (Unsigned driver)
Same root cause as 0x800B0109. Install the certificate and reinstall the driver.

### Device not recognized
- Ensure the device is in preloader/BROM mode (hold **Vol+** or **Vol−** while connecting USB)
- Try a different USB cable (short, high-quality cables work best)
- Use a USB 2.0 port directly (avoid USB 3.0 hubs)

### Driver won't install
- Run the install command as **Administrator**
- Make sure the `.cer` certificate file is present alongside the `.inf`
- Check Device Manager for error codes

### COM port appears briefly then disappears
- This is normal for BROM mode — the device only stays in this mode for ~1 second
- Use your flash tool's **auto-detect** or **scan** feature to catch the port automatically
- In SP Flash Tool, click Download first, then connect the device
