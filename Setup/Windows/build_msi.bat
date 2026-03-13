@echo off
REM ============================================================
REM  build_msi.bat – Build the mtkclient WinUSB driver MSI
REM
REM  Prerequisites:
REM    - WiX Toolset v3.x (https://wixtoolset.org/releases/)
REM      candle.exe and light.exe must be on PATH, or set WIX env.
REM    - A valid .cat catalog file in driver\ (use signtool / inf2cat)
REM
REM  Copyright (c) 2024-2025 mtkclient contributors – GPLv3
REM ============================================================

setlocal

set "SCRIPT_DIR=%~dp0"
set "DRIVER_DIR=%SCRIPT_DIR%driver"
set "INSTALLER_DIR=%SCRIPT_DIR%installer"
set "OUTPUT_DIR=%SCRIPT_DIR%output"
set "WXS_FILE=%INSTALLER_DIR%\mtkclient_driver.wxs"

echo.
echo ========================================
echo  Building mtkclient WinUSB Driver MSI
echo ========================================
echo.

REM --- Locate WiX tools ---
if defined WIX (
    set "CANDLE=%WIX%bin\candle.exe"
    set "LIGHT=%WIX%bin\light.exe"
) else (
    where candle.exe >nul 2>&1
    if %errorlevel% equ 0 (
        set "CANDLE=candle.exe"
        set "LIGHT=light.exe"
    ) else (
        echo ERROR: WiX Toolset not found.
        echo.
        echo Install WiX Toolset v3 from https://wixtoolset.org/releases/
        echo or set the WIX environment variable to the installation directory.
        pause
        exit /b 1
    )
)

REM --- Verify driver files exist ---
if not exist "%DRIVER_DIR%\mtkclient.inf" (
    echo ERROR: mtkclient.inf not found in %DRIVER_DIR%
    pause
    exit /b 1
)

REM --- Create a placeholder .cat if it doesn't exist ---
if not exist "%DRIVER_DIR%\mtkclient.cat" (
    echo NOTE: Creating placeholder catalog file.
    echo       For production, generate a proper .cat with inf2cat and sign it.
    echo. > "%DRIVER_DIR%\mtkclient.cat"
)

REM --- Create output directory ---
if not exist "%OUTPUT_DIR%" mkdir "%OUTPUT_DIR%"

REM --- Create RTF license file for WiX UI ---
if not exist "%SCRIPT_DIR%LICENSE.rtf" (
    echo Creating LICENSE.rtf from project LICENSE...
    powershell -NoProfile -Command "$t = Get-Content '%SCRIPT_DIR%..\..\LICENSE' -Raw; $r = '{\rtf1\ansi\deff0{\fonttbl{\f0 Consolas;}}\f0\fs18 ' + ($t -replace '\\','\\\\' -replace '\{','\{' -replace '\}','\}' -replace \"`r`n\",'\par ' -replace \"`n\",'\par ') + '}'; Set-Content '%SCRIPT_DIR%LICENSE.rtf' $r"
)

echo Compiling WiX source...
"%CANDLE%" -arch x64 -dDriverDir="%DRIVER_DIR%" -ext WixUtilExtension -out "%OUTPUT_DIR%\mtkclient_driver.wixobj" "%WXS_FILE%"
if %errorlevel% neq 0 (
    echo ERROR: WiX compilation failed.
    pause
    exit /b 1
)

echo Linking MSI...
"%LIGHT%" -ext WixUIExtension -ext WixUtilExtension -out "%OUTPUT_DIR%\mtkclient_driver.msi" "%OUTPUT_DIR%\mtkclient_driver.wixobj"
if %errorlevel% neq 0 (
    echo ERROR: WiX linking failed.
    pause
    exit /b 1
)

echo.
echo ========================================
echo  MSI created successfully:
echo  %OUTPUT_DIR%\mtkclient_driver.msi
echo ========================================
echo.
pause
exit /b 0
