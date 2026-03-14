<#
.SYNOPSIS
    Installs the MTK USB-to-Serial opensource KMDF driver.

.DESCRIPTION
    Registers the mtk_usb2ser.inf driver package with pnputil so that
    Windows recognises MediaTek loader USB devices automatically.

    Supported modes:
      - BROM       (VID 0E8D, PID 0003)
      - Preloader  (VID 0E8D, PID 2000)
      - DA         (VID 0E8D, PID 2001)

.NOTES
    Must be run as Administrator.
    SPDX-License-Identifier: MIT
#>

#Requires -RunAsAdministrator
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# ---------------------------------------------------------------------------
# Locate the INF file
# ---------------------------------------------------------------------------
$candidatePaths = @(
    (Join-Path $PSScriptRoot '..\driver\opensource\mtk_usb2ser.inf'),
    (Join-Path $PSScriptRoot '..\..\driver\opensource\mtk_usb2ser.inf'),
    (Join-Path $PSScriptRoot 'mtk_usb2ser.inf'),
    (Join-Path $PSScriptRoot '..\mtk_usb2ser.inf')
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
    Write-Error "Could not find mtk_usb2ser.inf. Searched:`n$($candidatePaths -join "`n")"
    exit 1
}

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

if ($enumOutput -match 'mtk_usb2ser\.inf') {
    Write-Host "`n[OK] Driver installed successfully." -ForegroundColor Green
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
  The following MediaTek USB devices are now supported:

    BROM       - VID 0E8D, PID 0003
    Preloader  - VID 0E8D, PID 2000
    DA         - VID 0E8D, PID 2001
    VCOM       - VID 0E8D, PID 2006 (MI_02)
    VCOM       - VID 0E8D, PID 2007

  Connect your device and check Device Manager
  under "Ports (COM & LPT)".
========================================================
"@ -ForegroundColor Green
