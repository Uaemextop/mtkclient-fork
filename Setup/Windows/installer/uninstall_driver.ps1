<#
.SYNOPSIS
    Uninstalls the MTK USB-to-Serial opensource KMDF driver.

.DESCRIPTION
    Finds the OEM driver-store entry for mtk_usb2ser.inf and removes it
    using pnputil.  Also cleans up stale SERIALCOMM registry values that
    reference "cdcacm" entries left behind by the driver.

.NOTES
    Must be run as Administrator.
    SPDX-License-Identifier: MIT
#>

#Requires -RunAsAdministrator
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# ---------------------------------------------------------------------------
# Enumerate drivers and find the OEM entry for mtk_usb2ser.inf
# ---------------------------------------------------------------------------
Write-Host "Searching for mtk_usb2ser.inf in the driver store ..." -ForegroundColor Yellow

$enumOutput = & pnputil.exe /enum-drivers 2>&1 | Out-String

# pnputil output contains blocks like:
#   Published Name:     oemXX.inf
#   Original Name:      mtk_usb2ser.inf
# We look for the OEM name associated with our INF.
$oemName = $null
$lines = $enumOutput -split "`r?`n"
for ($i = 0; $i -lt $lines.Count; $i++) {
    if ($lines[$i] -match 'Original Name\s*:\s*mtk_usb2ser\.inf') {
        # Walk backwards to find the Published Name line
        for ($j = $i - 1; $j -ge 0 -and $j -ge ($i - 5); $j--) {
            if ($lines[$j] -match 'Published Name\s*:\s*(oem\d+\.inf)') {
                $oemName = $Matches[1]
                break
            }
        }
        if ($oemName) { break }
    }
}

if (-not $oemName) {
    Write-Host "`n[INFO] mtk_usb2ser.inf is not currently installed in the driver store." -ForegroundColor Cyan
    exit 0
}

Write-Host "Found driver store entry: $oemName" -ForegroundColor Cyan

# ---------------------------------------------------------------------------
# Remove the driver package
# ---------------------------------------------------------------------------
Write-Host "`nRemoving driver package ..." -ForegroundColor Yellow

$removeResult = & pnputil.exe /delete-driver $oemName /uninstall /force 2>&1
$removeExitCode = $LASTEXITCODE

Write-Host ($removeResult | Out-String)

if ($removeExitCode -ne 0) {
    Write-Error "pnputil /delete-driver failed with exit code $removeExitCode."
    exit $removeExitCode
}

Write-Host "[OK] Driver package removed." -ForegroundColor Green

# ---------------------------------------------------------------------------
# Clean up SERIALCOMM registry entries containing "cdcacm"
# ---------------------------------------------------------------------------
$serialCommKey = 'HKLM:\HARDWARE\DEVICEMAP\SERIALCOMM'

if (Test-Path -LiteralPath $serialCommKey) {
    Write-Host "`nCleaning up SERIALCOMM registry entries ..." -ForegroundColor Yellow

    $key = Get-Item -LiteralPath $serialCommKey
    $removed = 0

    foreach ($valueName in $key.GetValueNames()) {
        if ($valueName -match 'cdcacm') {
            Write-Host "  Removing: $valueName" -ForegroundColor DarkGray
            Remove-ItemProperty -LiteralPath $serialCommKey -Name $valueName -Force
            $removed++
        }
    }

    if ($removed -eq 0) {
        Write-Host "  No cdcacm entries found." -ForegroundColor DarkGray
    } else {
        Write-Host "  Removed $removed SERIALCOMM entries." -ForegroundColor Cyan
    }
} else {
    Write-Host "`nSERIALCOMM registry key not present; nothing to clean." -ForegroundColor DarkGray
}

# ---------------------------------------------------------------------------
# Done
# ---------------------------------------------------------------------------
Write-Host @"

========================================================
  MTK USB-to-Serial Driver Uninstalled
========================================================
  The driver has been removed from the driver store.
  Reconnect any MediaTek devices so Windows can
  re-evaluate which driver to use.
========================================================
"@ -ForegroundColor Green
