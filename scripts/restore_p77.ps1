# SPDX-License-Identifier: MIT
# Recover violet from the 2026-11-04 p77 bootloop.
#
# What happened: a 97280-byte BOOTIMG.BIN with ANDROID! magic but an x86-64 ELF
# "kernel" was flashed to p77/recovery and verified byte-for-byte. The miscBCB
# still says boot-recovery, so the bootloader keeps trying to execute that ELF,
# fails, and the phone reboots. p76 (the Android boot slot) was NEVER written.
#
# The fix is to put the last known-good p77 image back, via fastboot. This script
# writes ONLY the recovery partition. It never touches p76/boot, never erases
# userdata, and never writes the fake skeleton image.
#
# Usage (dry run first, then commit with explicit consent):
#   powershell -ExecutionPolicy Bypass -File scripts\restore_p77.ps1
#   powershell -ExecutionPolicy Bypass -File scripts\restore_p77.ps1 -Execute -ConfirmPartitionWrite
# -Execute alone is NOT consent: fastboot flash recovery writes to the p77
# partition, so a second, explicit -ConfirmPartitionWrite switch is required.
# This is deliberately non-interactive (no Read-Host) so it works in CI too.
# Default image is the current m48 no-Android build: stock kernel, the storage
# init that publishes the five QEAFRESCUE folders, and the 66 .vqeaf themes.
# Pass -Image with any other validated p77 image to restore that instead.
#
# If the phone must boot Android immediately instead of rescue OS, add
# -AlsoClearMisc to also clear the bootloader control block (writes `misc`,
# not p77). Ask the user before using that switch.

param(
    [string]$Image = "out\boot-qeaf-violet-m48-noandroid.img",
    [switch]$Execute,
    [switch]$ConfirmPartitionWrite,
    [switch]$AlsoClearMisc
)

# fastboot writes normal getvar/progress output to stderr. With "Stop",
# PowerShell 5.1 converts those lines into a terminating NativeCommandError
# and aborts before the flash ever runs. Every real failure here is checked
# explicitly via $LASTEXITCODE, and Abort() throws on purpose, so "Continue"
# is the correct setting for these native calls.
$ErrorActionPreference = "Continue"
$ROOT = Split-Path -Parent $PSScriptRoot
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$logPath = Join-Path $ROOT "out\logs\restore-p77-$stamp.log"
New-Item -ItemType Directory -Path (Split-Path -Parent $logPath) -Force | Out-Null
$script:log = @()

function Note($msg) {
    $script:log += $msg
    Write-Host $msg
}

function Abort($msg) {
    Note ("ABORT: " + $msg)
    $script:log | Out-File -FilePath $logPath -Encoding utf8
    throw $msg
}

Note "=== violet p77 recovery / $stamp ==="
if ($Execute -and -not $ConfirmPartitionWrite) {
    Note "PARTITION WRITE: fastboot flash recovery writes p77/recovery."
    Note "REFUSED: -Execute without -ConfirmPartitionWrite. Nothing was written."
    $script:log | Out-File -FilePath $logPath -Encoding utf8
    exit 2
}
if ($ConfirmPartitionWrite) {
    Note "consent: -ConfirmPartitionWrite given (a p77 write is allowed)."
}

# 1. The image must pass the same structural gate used before any rescue flash.
$imgPath = Join-Path $ROOT $Image
if (-not (Test-Path $imgPath)) { Abort "image not found: $imgPath" }
& python (Join-Path $ROOT "scripts\validate_bootimg.py") $imgPath 2>&1 | ForEach-Object { Note $_ }
if ($LASTEXITCODE -ne 0) {
    Abort "image failed validate_bootimg.py - refusing to flash anything"
}
Note ("image size: " + (Get-Item $imgPath).Length + " bytes (p77 is 67108864)")

# 2. Find fastboot and a single violet device.
$fb = Get-Command fastboot -ErrorAction SilentlyContinue
if (-not $fb) { Abort "fastboot not on PATH. Install platform-tools or add it to PATH." }
$devices = @(& fastboot devices 2>&1 | Where-Object { $_ -match '\S' })
Note ("fastboot devices: " + $(if ($devices.Count -eq 0) { "(none)" } else { $devices -join ' ; ' }))
if ($devices.Count -eq 0) {
    Note "No fastboot device. On violet: power off, hold Volume Down + Power for ~8 s"
    Note "until the fastboot rabbit appears, then replug USB and re-run this script."
    Note "Do NOT flash until the device is visible here."
    $script:log | Out-File -FilePath $logPath -Encoding utf8
    if (-not $Execute) { Write-Host "(dry run) nothing written."; exit 0 }
    Abort "no device in fastboot"
}
if ($devices.Count -ne 1) { Abort "expected exactly one fastboot device, got $($devices.Count)" }

$product = (& fastboot getvar product 2>&1 | Out-String)
Note ("product: " + ($product.Trim() -replace '\s+', ' '))
if ($product -notmatch 'violet') {
    Abort "connected device is not violet - refusing to flash this image"
}

# 3. Plan. Nothing outside p77 (plus optional misc) is ever written.
$plan = @("fastboot flash recovery `"$imgPath`"")
if ($AlsoClearMisc) { $plan += "fastboot erase misc" }
$plan += "fastboot reboot recovery"
Note "--- planned commands ---"
foreach ($line in $plan) { Note $line }
Note "--- never run: fastboot flash boot / flash boot_a / erase userdata ---"
Note "PARTITION WRITE: 'fastboot flash recovery' writes p77/recovery."
Note "Consent required: -ConfirmPartitionWrite (in addition to -Execute)."

if (-not $Execute) {
    Note "(dry run) nothing written. Re-run with -Execute -ConfirmPartitionWrite once you are sure."
    $script:log | Out-File -FilePath $logPath -Encoding utf8
    exit 0
}

# 4. Execute, one p77 write, then reboot into the restored rescue OS.
Note "--- executing ---"
& fastboot flash recovery $imgPath 2>&1 | ForEach-Object { Note $_ }
if ($LASTEXITCODE -ne 0) { Abort "fastboot flash recovery failed - do not reboot yet, re-run" }
Note "flash recovery returned OK"

if ($AlsoClearMisc) {
    & fastboot erase misc 2>&1 | ForEach-Object { Note $_ }
    if ($LASTEXITCODE -ne 0) { Note "warn: erase misc failed (p77 is still restored)" }
}

& fastboot reboot recovery 2>&1 | ForEach-Object { Note $_ }
Note "reboot recovery issued."

Note "--- verification ---"
Note "1. wait ~20 s; the QEAFRESCUE FAT16 LUN should appear as a drive."
Note "2. read RESPONSE.TXT / QEAFBOOT.LOG of the NEW boot by raw sector."
Note "3. if you want the byte-for-byte proof, re-flash the same image through"
Note "   scripts\prepare_rescue_flash.py --volume <drive> --commit and require"
Note "   'flash: written and verified byte for byte' + 'lun: verified ...'."
Note "4. do not -Eject the volume; do not read /proc/tp_*."
$script:log | Out-File -FilePath $logPath -Encoding utf8
Write-Host "log: $logPath"
