@echo off
REM ============================================================
REM  install_driver.bat – Install the mtkclient WinUSB driver
REM  Must be run as Administrator on Windows 10/11 x64.
REM
REM  For test-signed drivers, this script also installs the test
REM  certificate to the Trusted Root and Trusted Publisher stores.
REM
REM  Copyright (c) 2024-2025 mtkclient contributors – GPLv3
REM ============================================================

setlocal

echo.
echo ========================================
echo  mtkclient WinUSB Driver Installer
echo ========================================
echo.

REM --- Check for Administrator privileges ---
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo ERROR: This script must be run as Administrator.
    echo Right-click the script and select "Run as administrator".
    pause
    exit /b 1
)

REM --- Locate the INF file relative to this script ---
set "SCRIPT_DIR=%~dp0"
set "INF_PATH=%SCRIPT_DIR%driver\mtkclient.inf"
set "CERT_PATH=%SCRIPT_DIR%output\mtkclient_test_cert.cer"

if not exist "%INF_PATH%" (
    echo ERROR: Driver file not found at:
    echo   %INF_PATH%
    echo.
    echo Make sure you are running this script from the Setup\Windows directory.
    pause
    exit /b 1
)

REM --- Install test certificate if present ---
if exist "%CERT_PATH%" (
    echo Installing test signing certificate...
    echo Certificate: %CERT_PATH%
    echo.
    certutil -addstore "Root" "%CERT_PATH%" >nul 2>&1
    if %errorlevel% equ 0 (
        echo   - Added to Trusted Root Certification Authorities
    ) else (
        echo   - WARNING: Could not add to Root store
    )
    certutil -addstore "TrustedPublisher" "%CERT_PATH%" >nul 2>&1
    if %errorlevel% equ 0 (
        echo   - Added to Trusted Publishers
    ) else (
        echo   - WARNING: Could not add to Trusted Publisher store
    )
    echo.
) else (
    echo NOTE: No test certificate found at %CERT_PATH%
    echo       If the driver is test-signed, install mtkclient_test_cert.cer
    echo       manually before installing the driver.
    echo.
)

echo Installing WinUSB driver for MediaTek devices...
echo INF: %INF_PATH%
echo.

REM --- Use pnputil to add and install the driver ---
pnputil /add-driver "%INF_PATH%" /install

if %errorlevel% equ 0 (
    echo.
    echo SUCCESS: Driver installed successfully.
    echo.
    echo The WinUSB driver is now associated with MediaTek bootrom
    echo and preloader USB devices. You no longer need UsbDk.
    echo.
    echo Supported devices:
    echo   - MTK Bootrom ^(VID:0x0E8D PID:0x0003^)
    echo   - MTK Preloader ^(VID:0x0E8D PID:0x2000/0x2001/0x20FF/0x3000/0x6000^)
    echo   - LG Preloader ^(VID:0x1004 PID:0x6000^)
    echo   - OPPO Preloader ^(VID:0x22D9 PID:0x0006^)
    echo.
    echo Connect your device and mtkclient should detect it automatically.
) else (
    echo.
    echo WARNING: Driver installation returned error code %errorlevel%.
    echo This may be normal if the device is not currently connected.
    echo The driver has been staged and will bind automatically when
    echo a matching device is plugged in.
)

echo.
pause
exit /b 0
