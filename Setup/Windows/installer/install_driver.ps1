<#
.SYNOPSIS
    Installs the MTK USB-to-Serial driver (KMDF or CDC with usb2ser.sys).

.DESCRIPTION
    Registers the mtk_usb2ser.inf (KMDF) or mtk_preloader_opensource.inf (CDC
    with usb2ser.sys) driver package with pnputil so that Windows recognises
    MediaTek loader USB devices automatically as COM ports.

    Once installed, mtkclient works without libusb or UsbDk.

    Supported modes:
      - BROM       (VID 0E8D, PID 0003)
      - Preloader  (VID 0E8D, PID 2000)
      - DA         (VID 0E8D, PID 2001)
      - VCOM/Meta  (VID 0E8D, PID 2006-2064)
      - ETS/ELT    (composite interfaces)
      - C2K Modem  (composite interfaces)

.NOTES
    Must be run as Administrator.
    SPDX-License-Identifier: MIT
#>

#Requires -RunAsAdministrator
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# ---------------------------------------------------------------------------
# Locate the INF file — try KMDF first, then CDC with usb2ser.sys
# ---------------------------------------------------------------------------
$candidatePaths = @(
    # KMDF driver (Option A)
    (Join-Path $PSScriptRoot '..\driver\opensource\mtk_usb2ser.inf'),
    (Join-Path $PSScriptRoot '..\..\driver\opensource\mtk_usb2ser.inf'),
    (Join-Path $PSScriptRoot 'mtk_usb2ser.inf'),
    # CDC driver with usb2ser.sys (Option B)
    (Join-Path $PSScriptRoot '..\driver\CDC\mtk_preloader_opensource.inf'),
    (Join-Path $PSScriptRoot '..\..\driver\CDC\mtk_preloader_opensource.inf'),
    (Join-Path $PSScriptRoot 'mtk_preloader_opensource.inf')
)

$infPath = $null
foreach ($candidate in $candidatePaths) {
    $resolved = [System.IO.Path]::GetFullPath($candidate)
    if (Test-Path -LiteralPath $resolved -PathType Leaf) {
        $infPath = $resolved
        break
    }
}

if (-not $infPath) {
    Write-Error "Could not find mtk_usb2ser.inf or mtk_preloader_opensource.inf. Searched:`n$($candidatePaths -join "`n")"
    exit 1
}

$infName = [System.IO.Path]::GetFileName($infPath)
Write-Host "Found INF: $infPath" -ForegroundColor Cyan

# ---------------------------------------------------------------------------
# Install the driver package
# ---------------------------------------------------------------------------
Write-Host "`nInstalling driver package with pnputil ..." -ForegroundColor Yellow

$pnpResult = & pnputil.exe /add-driver "$infPath" /install 2>&1
$pnpExitCode = $LASTEXITCODE

Write-Host ($pnpResult | Out-String)

if ($pnpExitCode -ne 0) {
    Write-Error "pnputil /add-driver failed with exit code $pnpExitCode."
    exit $pnpExitCode
}

# ---------------------------------------------------------------------------
# Verify installation
# ---------------------------------------------------------------------------
Write-Host "Verifying installation ..." -ForegroundColor Yellow

$enumOutput = & pnputil.exe /enum-drivers 2>&1 | Out-String

$searchPattern = [System.IO.Path]::GetFileNameWithoutExtension($infName)
if ($enumOutput -match $searchPattern) {
    Write-Host "`n[OK] Driver installed successfully ($infName)." -ForegroundColor Green
} else {
    Write-Warning "Driver was added but could not be confirmed in the driver store."
}

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
Write-Host @"

========================================================
  MTK USB-to-Serial Driver Installed
========================================================
  Driver: $infName
  No libusb or UsbDk required.

  The following MediaTek USB devices are now supported:

    BROM       - VID 0E8D, PID 0003
    Preloader  - VID 0E8D, PID 2000
    DA         - VID 0E8D, PID 2001
    VCOM       - VID 0E8D, PID 2006-2064 (Meta/Composite)
    ETS/ELT    - Engineering ports
    C2K Modem  - CDMA2000 composite

  Connect your device and check Device Manager
  under "Ports (COM & LPT)".

  mtkclient will auto-detect the COM port on Windows.
========================================================
"@ -ForegroundColor Green
