@echo off
REM ============================================================
REM  uninstall_driver.bat – Remove the mtkclient WinUSB driver
REM  Must be run as Administrator on Windows 10/11 x64.
REM
REM  Copyright (c) 2024-2025 mtkclient contributors – GPLv3
REM ============================================================

setlocal

echo.
echo ========================================
echo  mtkclient WinUSB Driver Uninstaller
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

set "SCRIPT_DIR=%~dp0"
set "INF_PATH=%SCRIPT_DIR%driver\mtkclient.inf"

echo Removing WinUSB driver for MediaTek devices...
echo.

REM --- Find and remove staged driver by OEM inf name ---
for /f "tokens=1" %%i in ('pnputil /enum-drivers ^| findstr /i "mtkclient"') do (
    echo Removing published driver: %%i
    pnputil /delete-driver %%i /uninstall /force
)

REM --- Also try direct removal ---
if exist "%INF_PATH%" (
    pnputil /delete-driver "%INF_PATH%" /uninstall /force 2>nul
)

echo.
echo Driver removal complete.
echo.
pause
exit /b 0
