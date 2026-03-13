@echo off
REM ============================================================
REM  install_driver.bat – Install the mtkclient WinUSB driver
REM  Must be run as Administrator on Windows 10/11 x64.
REM
REM  For test-signed drivers, this script:
REM    1. Installs the test certificate to Trusted Root + Publisher
REM    2. Enables Windows Test Signing Mode (bcdedit)
REM    3. Stages the driver via pnputil
REM
REM  A REBOOT is required after the first run for test signing
REM  mode to take effect.
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
set "NEED_REBOOT=0"

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
    echo [1/3] Installing test signing certificate...
    echo       Certificate: %CERT_PATH%
    echo.
    certutil -addstore "Root" "%CERT_PATH%" >nul 2>&1
    if %errorlevel% equ 0 (
        echo   + Added to Trusted Root Certification Authorities
    ) else (
        echo   - WARNING: Could not add to Root store ^(may already exist^)
    )
    certutil -addstore "TrustedPublisher" "%CERT_PATH%" >nul 2>&1
    if %errorlevel% equ 0 (
        echo   + Added to Trusted Publishers
    ) else (
        echo   - WARNING: Could not add to Trusted Publisher store ^(may already exist^)
    )
    echo.

    REM --- Enable test signing mode ---
    echo [2/3] Enabling Windows Test Signing Mode...
    echo       (Required for self-signed/test-signed drivers)
    echo.
    bcdedit /set testsigning on >nul 2>&1
    if %errorlevel% equ 0 (
        echo   + Test signing mode enabled
        set "NEED_REBOOT=1"
    ) else (
        echo   - NOTE: Could not enable test signing.
        echo          If Secure Boot is enabled in BIOS, you may need to
        echo          disable it first, or use a production-signed driver.
    )
    echo.
) else (
    echo NOTE: No test certificate found at:
    echo   %CERT_PATH%
    echo.
    echo If using the CI build artifacts, extract the .cer file
    echo to Setup\Windows\output\ before running this script.
    echo.
)

echo [3/3] Installing WinUSB driver for MediaTek devices...
echo       INF: %INF_PATH%
echo.

REM --- Remove any previously staged version first ---
for /f "tokens=1" %%i in ('pnputil /enum-drivers ^| findstr /i "mtkclient"') do (
    pnputil /delete-driver %%i /force >nul 2>&1
)

REM --- Use pnputil to add and install the driver ---
pnputil /add-driver "%INF_PATH%" /install

if %errorlevel% equ 0 (
    echo.
    echo ========================================
    echo  SUCCESS: Driver installed successfully.
    echo ========================================
    echo.
    echo The WinUSB driver is now associated with MediaTek devices:
    echo   - MTK Bootrom  (VID:0x0E8D PID:0x0003^)
    echo   - MTK Preloader (VID:0x0E8D PID:0x2000/0x2001/0x20FF/0x3000/0x6000^)
    echo   - MTK Meta Mode (VID:0x0E8D PID:0x2007^)
    echo   - LG Preloader  (VID:0x1004 PID:0x6000^)
    echo   - OPPO Preloader (VID:0x22D9 PID:0x0006^)
    echo.
) else (
    echo.
    echo WARNING: pnputil returned error code %errorlevel%.
    echo This may be normal if the device is not currently connected.
    echo The driver has been staged and will bind automatically when
    echo a matching device is plugged in.
    echo.
)

if "%NEED_REBOOT%"=="1" (
    echo ****************************************
    echo  IMPORTANT: A REBOOT IS REQUIRED
    echo  Test signing mode was just enabled.
    echo  Please restart your computer before
    echo  connecting your MediaTek device.
    echo ****************************************
    echo.
)

pause
exit /b 0
