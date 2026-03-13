@echo off
:: Build mtkclient MSI installer using WiX Toolset
::
:: Prerequisites:
::   - WiX Toolset v3.x installed (https://wixtoolset.org/)
::   - Built executables in dist\ directory
::   - Built DLL in mtkclient\Windows\
::
:: Usage: build_msi.bat [Release|Debug]

setlocal enabledelayedexpansion

set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Release"

set "SCRIPT_DIR=%~dp0"
set "ROOT_DIR=%SCRIPT_DIR%..\.."
set "WXS_FILE=%SCRIPT_DIR%installer\mtkclient.wxs"
set "BUILD_DIR=%SCRIPT_DIR%build_msi"

:: Locate WiX tools
set "WIX_DIR="
if defined WIX set "WIX_DIR=%WIX%bin\"
if not exist "%WIX_DIR%candle.exe" (
    for %%d in (
        "C:\Program Files (x86)\WiX Toolset v3.14\bin"
        "C:\Program Files (x86)\WiX Toolset v3.11\bin"
        "C:\Program Files\WiX Toolset v3.14\bin"
    ) do (
        if exist "%%~d\candle.exe" set "WIX_DIR=%%~d\"
    )
)

if not exist "%WIX_DIR%candle.exe" (
    echo ERROR: WiX Toolset not found.
    echo Install from: https://wixtoolset.org/
    echo Or set WIX environment variable to the install directory.
    exit /b 1
)

echo WiX Toolset: %WIX_DIR%

:: Create build directory
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

:: Source directories (adjust these paths based on your build output)
set "DIST_DIR=%ROOT_DIR%\dist"
set "DLL_DIR=%ROOT_DIR%\mtkclient\Windows"
set "WINDOWS_DIR=%ROOT_DIR%\mtkclient\Windows"
set "DRIVER_DIR=%ROOT_DIR%\Setup\Windows\driver"

:: Verify required files
echo.
echo Checking required files...
set "MISSING=0"

if not exist "%DIST_DIR%\mtk_console.exe" (
    echo   MISSING: %DIST_DIR%\mtk_console.exe
    set "MISSING=1"
)
if not exist "%DLL_DIR%\mtk_usb_driver.dll" (
    echo   WARNING: %DLL_DIR%\mtk_usb_driver.dll (optional)
)
if not exist "%WINDOWS_DIR%\libusb-1.0.dll" (
    echo   WARNING: %WINDOWS_DIR%\libusb-1.0.dll (optional with WinUSB)
)
if not exist "%DRIVER_DIR%\mtkclient_winusb.inf" (
    echo   MISSING: %DRIVER_DIR%\mtkclient_winusb.inf
    set "MISSING=1"
)

if "%MISSING%"=="1" (
    echo.
    echo ERROR: Required files are missing. Build the project first.
    exit /b 1
)

echo   All required files found.

:: Compile WiX source
echo.
echo Compiling WiX source...
"%WIX_DIR%candle.exe" ^
    -dDistDir="%DIST_DIR%" ^
    -dDllDir="%DLL_DIR%" ^
    -dWindowsDir="%WINDOWS_DIR%" ^
    -dDriverDir="%DRIVER_DIR%" ^
    -arch x64 ^
    -out "%BUILD_DIR%\mtkclient.wixobj" ^
    "%WXS_FILE%"

if %errorlevel% neq 0 (
    echo ERROR: WiX compilation failed.
    exit /b 1
)

:: Link MSI
echo.
echo Linking MSI...
"%WIX_DIR%light.exe" ^
    -ext WixUIExtension ^
    -out "%BUILD_DIR%\mtkclient_setup.msi" ^
    "%BUILD_DIR%\mtkclient.wixobj"

if %errorlevel% neq 0 (
    echo ERROR: WiX linking failed.
    exit /b 1
)

echo.
echo SUCCESS: MSI built at %BUILD_DIR%\mtkclient_setup.msi
echo.

:: Copy to root for easy access
copy "%BUILD_DIR%\mtkclient_setup.msi" "%ROOT_DIR%\mtkclient_setup.msi" >nul 2>&1

exit /b 0
