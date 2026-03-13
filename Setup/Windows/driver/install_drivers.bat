@echo off
:: install_drivers.bat — Install mtkclient WinUSB + Serial drivers
:: Derived from MTK SP Drivers 20160804, repackaged for Windows 10/11 x64
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
echo   Derived from MTK SP Drivers 20160804
echo ============================================================
echo.

set "DRIVER_DIR=%~dp0"

echo [1/2] Installing WinUSB driver (BROM, Preloader, DA, ADB)...
pnputil /add-driver "%DRIVER_DIR%mtkclient_winusb.inf" /install
if %errorlevel% neq 0 (
    echo WARNING: WinUSB driver installation returned error %errorlevel%
) else (
    echo       WinUSB driver installed successfully.
)
echo.

echo [2/2] Installing Serial driver (VCOM, Meta, ETS, ELT)...
pnputil /add-driver "%DRIVER_DIR%mtkclient_preloader.inf" /install
if %errorlevel% neq 0 (
    echo WARNING: Serial driver installation returned error %errorlevel%
) else (
    echo       Serial driver installed successfully.
)
echo.

echo ============================================================
echo   Installation complete.
echo   Connect your MediaTek device in BROM or Preloader mode
echo   and check Device Manager for proper detection.
echo ============================================================
pause
