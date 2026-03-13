@echo off
REM ============================================================
REM  build_msi.bat – Build the mtkclient WinUSB driver MSI
REM
REM  Prerequisites:
REM    - WiX Toolset v3.x (https://wixtoolset.org/releases/)
REM      candle.exe and light.exe must be on PATH, or set WIX env.
REM    - PowerShell 5.1+ (for New-FileCatalog and code signing)
REM
REM  This script will:
REM    1. Create a self-signed test certificate (if no cert provided)
REM    2. Generate and sign the driver catalog (.cat)
REM    3. Build the MSI installer
REM    4. Sign the MSI installer
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

REM --- Create output directory ---
if not exist "%OUTPUT_DIR%" mkdir "%OUTPUT_DIR%"

REM --- Generate and sign driver catalog ---
echo Generating and signing driver catalog...
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$stagingDir = Join-Path $env:TEMP 'mtkclient_staging';" ^
  "if (Test-Path $stagingDir) { Remove-Item $stagingDir -Recurse -Force };" ^
  "New-Item -ItemType Directory -Force -Path $stagingDir | Out-Null;" ^
  "Copy-Item '%DRIVER_DIR%\mtkclient.inf' $stagingDir\;" ^
  "$cert = Get-ChildItem Cert:\CurrentUser\My -CodeSigningCert | Select-Object -First 1;" ^
  "if (-not $cert) {" ^
  "  Write-Host 'No code signing certificate found — creating self-signed test cert...';" ^
  "  $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject 'CN=mtkclient Test Signing Authority' -FriendlyName 'mtkclient Driver Test Certificate' -CertStoreLocation Cert:\CurrentUser\My -NotAfter (Get-Date).AddYears(5) -HashAlgorithm SHA256;" ^
  "  Export-Certificate -Cert $cert -FilePath '%OUTPUT_DIR%\mtkclient_test_cert.cer' | Out-Null;" ^
  "  Write-Host \"Exported test certificate to %OUTPUT_DIR%\mtkclient_test_cert.cer\";" ^
  "};" ^
  "New-FileCatalog -Path $stagingDir -CatalogFilePath (Join-Path $stagingDir 'mtkclient.cat') -CatalogVersion 2.0;" ^
  "$sig = Set-AuthenticodeSignature -FilePath (Join-Path $stagingDir 'mtkclient.cat') -Certificate $cert -HashAlgorithm SHA256;" ^
  "Write-Host \"Catalog signature: $($sig.Status)\";" ^
  "Copy-Item (Join-Path $stagingDir 'mtkclient.cat') '%DRIVER_DIR%\' -Force;" ^
  "Write-Host 'Signed catalog placed in driver directory';"

if %errorlevel% neq 0 (
    echo ERROR: Catalog generation/signing failed.
    pause
    exit /b 1
)

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

REM --- Sign the MSI ---
echo Signing MSI installer...
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$cert = Get-ChildItem Cert:\CurrentUser\My -CodeSigningCert | Select-Object -First 1;" ^
  "if ($cert) {" ^
  "  $sig = Set-AuthenticodeSignature -FilePath '%OUTPUT_DIR%\mtkclient_driver.msi' -Certificate $cert -HashAlgorithm SHA256;" ^
  "  Write-Host \"MSI signature: $($sig.Status)\";" ^
  "} else { Write-Host 'WARNING: No signing certificate found, MSI is unsigned' };"

echo.
echo ========================================
echo  MSI created successfully:
echo  %OUTPUT_DIR%\mtkclient_driver.msi
echo ========================================
echo.
pause
exit /b 0
