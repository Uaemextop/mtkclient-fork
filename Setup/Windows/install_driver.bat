@echo off
:: mtkclient WinUSB Driver Installation Script
:: Installs the WinUSB driver for MediaTek Preloader/Bootrom devices
:: Must be run as Administrator
::
:: Usage: install_driver.bat [/winusb | /serial | /uninstall]
::   /winusb    - Install WinUSB driver (default, recommended for mtkclient)
::   /serial    - Install usbser.sys driver (COM port mode)
::   /uninstall - Remove installed driver

setlocal enabledelayedexpansion

:: Check admin privileges
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo ERROR: This script requires Administrator privileges.
    echo Right-click and select "Run as administrator"
    pause
    exit /b 1
)

set "SCRIPT_DIR=%~dp0"
set "MODE=winusb"

:: Parse arguments
if /i "%~1"=="/serial" set "MODE=serial"
if /i "%~1"=="/uninstall" set "MODE=uninstall"
if /i "%~1"=="/winusb" set "MODE=winusb"
if /i "%~1"=="/?" goto :usage
if /i "%~1"=="/help" goto :usage

if "%MODE%"=="uninstall" goto :uninstall

:: Determine which INF to install
if "%MODE%"=="winusb" (
    set "INF_FILE=%SCRIPT_DIR%driver\mtkclient_winusb.inf"
    set "DRIVER_DESC=WinUSB (direct USB access, no UsbDk/libusb needed)"
) else (
    set "INF_FILE=%SCRIPT_DIR%driver\mtkclient_preloader.inf"
    set "DRIVER_DESC=usbser.sys (COM port mode)"
)

if not exist "%INF_FILE%" (
    :: Try alternative paths
    set "INF_FILE=%SCRIPT_DIR%mtkclient_winusb.inf"
    if not exist "!INF_FILE!" (
        echo ERROR: Driver INF file not found.
        echo Expected: %INF_FILE%
        pause
        exit /b 1
    )
)

echo.
echo  mtkclient Driver Installer
echo  ==========================
echo.
echo  Mode: %MODE%
echo  Driver: %DRIVER_DESC%
echo  INF: %INF_FILE%
echo.

:: Install certificate if present
set "CERT_FILE=%SCRIPT_DIR%driver\mtkclient_cert.cer"
if exist "%CERT_FILE%" (
    echo Installing certificate...
    certutil -addstore "TrustedPublisher" "%CERT_FILE%" >nul 2>&1
    certutil -addstore "Root" "%CERT_FILE%" >nul 2>&1
    echo   Certificate installed.
) else (
    echo   No certificate file found (optional).
)

:: Install driver
echo.
echo Installing driver...
pnputil /add-driver "%INF_FILE%" /install
if %errorlevel% equ 0 (
    echo.
    echo  SUCCESS: Driver installed.
    echo  Connect your MediaTek device in bootrom/preloader mode.
    echo  It should appear as "%DRIVER_DESC%" in Device Manager.
) else (
    echo.
    echo  WARNING: pnputil returned error code %errorlevel%.
    echo  The driver may still work. Check Device Manager.
)

echo.
pause
exit /b 0

:uninstall
echo.
echo  Removing mtkclient drivers...
echo.

:: Remove WinUSB driver
for /f "tokens=2 delims= " %%i in ('pnputil /enum-drivers ^| findstr /i "mtkclient_winusb"') do (
    echo Removing: %%i
    pnputil /delete-driver %%i /uninstall /force 2>nul
)

:: Remove usbser driver
for /f "tokens=2 delims= " %%i in ('pnputil /enum-drivers ^| findstr /i "mtkclient_preloader"') do (
    echo Removing: %%i
    pnputil /delete-driver %%i /uninstall /force 2>nul
)

echo.
echo  Drivers removed. You may need to reconnect your device.
echo.
pause
exit /b 0

:usage
echo.
echo  mtkclient Driver Installer
echo.
echo  Usage: %~nx0 [/winusb ^| /serial ^| /uninstall]
echo.
echo  Options:
echo    /winusb    Install WinUSB driver (default, recommended)
echo               Enables direct USB access without UsbDk/libusb
echo    /serial    Install usbser.sys driver (COM port mode)
echo               Device appears as COM port in Device Manager
echo    /uninstall Remove all mtkclient drivers
echo    /help      Show this help message
echo.
exit /b 0
