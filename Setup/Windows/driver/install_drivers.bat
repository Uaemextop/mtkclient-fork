@echo off
:: install_drivers.bat — Install mtkclient USB drivers
:: ADB driver based on Google USB Driver r13, modified for MTK devices.
:: Serial driver uses inbox usbser.sys (replaces legacy usb2ser.sys).
::
:: Port speeds configured by the serial driver:
::   BROM/Preloader handshake : 115200 bps  8-N-1
::   DA high-speed transfer   : 921600 bps  8-N-1
::   No flow control (RTS/CTS and XON/XOFF disabled)
::
:: Run this script as Administrator.

net session >nul 2>&1
if %errorlevel% neq 0 (
    echo ERROR: This script must be run as Administrator.
    echo Right-click and select "Run as administrator".
    pause
    exit /b 1
)

echo ============================================================
echo   mtkclient Driver Installer — Windows 10/11 x64
echo   ADB driver: Google USB Driver r13 modified for MTK
echo   Serial driver: inbox usbser.sys (Win10+)
echo.
echo   Serial port config:
echo     Default baud : 115200 bps (BROM handshake)
echo     Max baud     : 921600 bps (DA transfer)
echo     Format       : 8-N-1, no flow control
echo ============================================================
echo.

set "DRIVER_DIR=%~dp0"

echo [1/5] Removing old MediaTek drivers and USB devices...
if exist "%DRIVER_DIR%cleanup_mtk_drivers.ps1" (
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%DRIVER_DIR%cleanup_mtk_drivers.ps1"
) else (
    echo       Cleanup script not found, skipping...
)
echo.

echo [2/5] Installing test-signing certificate...
if exist "%DRIVER_DIR%mtkclient_test.cer" (
    certutil -addstore Root "%DRIVER_DIR%mtkclient_test.cer"
    certutil -addstore TrustedPublisher "%DRIVER_DIR%mtkclient_test.cer"
    echo       Certificate installed to Root and TrustedPublisher stores.
) else (
    echo       Certificate file not found, skipping...
    echo       NOTE: Driver installation may fail without a trusted certificate.
)
echo.

echo [3/5] Installing ADB / Bootloader / MTP driver (Google r13 + MTK)...
pnputil /add-driver "%DRIVER_DIR%adb\android_winusb.inf" /install
if %errorlevel% neq 0 (
    echo WARNING: ADB driver installation returned error %errorlevel%
) else (
    echo       ADB driver installed successfully.
)
echo.

echo [4/5] Installing Serial driver (VCOM, Meta, ETS, ELT)...
echo       Configuring: 115200 bps default, 921600 bps max, 8-N-1
pnputil /add-driver "%DRIVER_DIR%CDC\cdc-acm.inf" /install
if %errorlevel% neq 0 (
    echo WARNING: Serial driver installation returned error %errorlevel%
) else (
    echo       Serial driver installed successfully.
)
echo.

echo [5/5] Verifying installation...
pnputil /enum-drivers | findstr /i "android_winusb cdc-acm" >nul 2>&1
if %errorlevel% equ 0 (
    echo       Drivers are registered in the Driver Store.
) else (
    echo       WARNING: Could not verify driver registration.
)
echo.

echo ============================================================
echo   Installation complete.
echo.
echo   Connect your MediaTek device and check Device Manager.
echo.
echo   Supported modes (VID 0x0E8D):
echo     BROM       : PID 0x0003
echo     Preloader  : PID 0x2000, 0x20FF, 0x3000, 0x6000
echo     DA         : PID 0x2001
echo     Bootloader : PID 0x2024
echo     ADB        : Various composite PIDs
echo     MTP        : PID 0x2008 + composites
echo     VCOM/Meta  : PID 0x2007 + composites
echo ============================================================
pause
