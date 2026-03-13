#Requires -RunAsAdministrator
<#
.SYNOPSIS
  Remove old MediaTek USB drivers and device instances for clean installation.

.DESCRIPTION
  This script is called by the MSI installer (or manually) before installing
  new mtkclient drivers.  It removes:
    1. Existing MediaTek USB device instances (VID 0x0E8D, 0x0FCE, 0x1004, 0x22D9)
    2. Old MTK OEM drivers from the Windows Driver Store
       (cdc-acm.inf, android_winusb.inf, usb2ser.sys variants)
#>

[CmdletBinding()]
param()

$ErrorActionPreference = 'SilentlyContinue'

# VIDs used by MTK devices (MediaTek, Sony, LG, OPPO)
$vidPatterns = @('USB\VID_0E8D*', 'USB\VID_0FCE*', 'USB\VID_1004*', 'USB\VID_22D9*')

# ── Step 1: Remove existing MTK USB device instances ──────────────────────
Write-Host "=== Removing existing MediaTek USB device instances ==="
foreach ($pattern in $vidPatterns) {
    $devices = Get-PnpDevice -InstanceId $pattern -ErrorAction SilentlyContinue
    if ($devices) {
        foreach ($dev in $devices) {
            Write-Host "  Removing: $($dev.InstanceId) [$($dev.FriendlyName)]"
            $result = & pnputil /remove-device $dev.InstanceId /subtree 2>&1
            if ($LASTEXITCODE -ne 0) {
                Write-Host "    WARNING: removal returned exit code $LASTEXITCODE (device may be in use)"
            }
        }
    }
}

# Also remove COM ports that belong to old MTK serial driver (usb2ser)
$comDevices = Get-PnpDevice -Class Ports -ErrorAction SilentlyContinue |
    Where-Object { $_.InstanceId -match 'VID_0E8D' }
if ($comDevices) {
    foreach ($dev in $comDevices) {
        Write-Host "  Removing COM: $($dev.InstanceId) [$($dev.FriendlyName)]"
        $result = & pnputil /remove-device $dev.InstanceId /subtree 2>&1
        if ($LASTEXITCODE -ne 0) {
            Write-Host "    WARNING: removal returned exit code $LASTEXITCODE"
        }
    }
}

# ── Step 2: Remove old MTK OEM drivers from Driver Store ──────────────────
Write-Host ""
Write-Host "=== Scanning Driver Store for old MediaTek drivers ==="

# Patterns that identify old MTK driver INFs
$oldDriverPatterns = @(
    'cdc-acm',
    'android_winusb',
    'usb2ser',
    'MediaTek.*USB2SER',
    'MTK\s+USB',
    'mtkmbim',
    'tetherxp'
)
$patternRegex = ($oldDriverPatterns -join '|')

$driverOutput = & pnputil /enum-drivers 2>&1 | Out-String

# Split output into blocks per driver entry
$blocks = $driverOutput -split '(?=Published Name\s*:)'
foreach ($block in $blocks) {
    if ($block -match "(?i)($patternRegex)" -and
        $block -match 'Published Name\s*:\s*(oem\d+\.inf)') {
        $oemInf = $Matches[1]
        Write-Host "  Removing old driver: $oemInf"
        $result = & pnputil /delete-driver $oemInf /force /uninstall 2>&1
        if ($LASTEXITCODE -ne 0) {
            Write-Host "    WARNING: delete-driver returned exit code $LASTEXITCODE (driver may be in use)"
        }
    }
}

Write-Host ""
Write-Host "=== Cleanup complete ==="
exit 0
