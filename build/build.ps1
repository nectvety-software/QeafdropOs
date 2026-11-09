# SPDX-License-Identifier: MIT
# Build script for UMOS/violet (PowerShell version)
# Usage: pwsh build/build.ps1
#        pwsh build/build.ps1 -P77Image out\boot-qeaf-violet-m48-noandroid.img
#
# This build compiles and links qeafinit.elf (a userspace ELF). It does NOT
# produce a flashable boot image from that ELF: doing so caused the 2026-11-04
# p77 bootloop. Pass -P77Image with a real, validated install image to stage one.

param(
    [string]$P77Image = ""
)

$ErrorActionPreference = "Stop"

$ROOT = Resolve-Path "$PSScriptRoot/.."
$OUT = Join-Path $ROOT "out"
$ZIG = "D:\QeafivelsOfficial\QeafdropOs\out\toolchain\zig-windows-x86_64-0.13.0\zig.exe"

Write-Host "Using zig: $ZIG" -ForegroundColor Cyan

if (-not (Test-Path $ZIG)) {
    Write-Error "zig compiler not found at $ZIG"
    exit 1
}

New-Item -ItemType Directory -Path $OUT -Force | Out-Null
$dirs = @(
    "$OUT/core", "$OUT/core/scheduler", "$OUT/core/memory",
    "$OUT/hal/src",
    "$OUT/bsp/violet",
    "$OUT/gui/gfx", "$OUT/gui/widgets",
    "$OUT/apps/launcher", "$OUT/apps/lock", "$OUT/apps/settings",
    "$OUT/apps/messages", "$OUT/apps/gallery", "$OUT/apps/browser",
    "$OUT/apps/recovery", "$OUT/apps/bootui", "$OUT/apps/fastboot",
    "$OUT/apps/usbui", "$OUT/apps/keyboard",
    "$OUT/fs", "$OUT/init"
)
foreach ($d in $dirs) { New-Item -ItemType Directory -Path $d -Force | Out-Null }

$INCLUDE_FLAGS = @(
    "-I$ROOT", "-I$ROOT/hal/include", "-I$ROOT/core", "-I$ROOT/core/scheduler",
    "-I$ROOT/core/memory", "-I$ROOT/bsp/violet", "-I$ROOT/init",
    "-I$ROOT/gui/gfx", "-I$ROOT/gui/widgets", "-I$ROOT/fs"
)
$BASE_FLAGS = @("-target", "aarch64-linux-musl", "-ffreestanding", "-fno-builtin",
                "-fno-stack-protector", "-O2")

function Compile($src, $obj) {
    Write-Host "  Compiling $src" -ForegroundColor Gray
    $allArgs = @("cc") + $BASE_FLAGS + $INCLUDE_FLAGS + @("-c", $src, "-o", $obj)
    & $ZIG @allArgs
    if ($LASTEXITCODE -ne 0) { Write-Error "Compilation failed: $src" }
}

# Core
Write-Host "Compiling core..." -ForegroundColor Cyan
Compile "$ROOT/core/log.c" "$OUT/core/log.o"
Compile "$ROOT/core/event_queue.c" "$OUT/core/event_queue.o"
Compile "$ROOT/core/scheduler/scheduler.c" "$OUT/core/scheduler/scheduler.o"
Compile "$ROOT/core/memory/heap.c" "$OUT/core/memory/heap.o"
Compile "$ROOT/core/sysio.c" "$OUT/core/sysio.o"
Compile "$ROOT/core/platform.c" "$OUT/core/platform.o"
Compile "$ROOT/core/clock.c" "$OUT/core/clock.o"
Compile "$ROOT/core/i18n.c" "$OUT/core/i18n.o"
Compile "$ROOT/core/prefs.c" "$OUT/core/prefs.o"

# HAL
Write-Host "Compiling HAL..." -ForegroundColor Cyan
Compile "$ROOT/hal/src/hal_manager.c" "$OUT/hal/src/hal_manager.o"

# BSP violet
Write-Host "Compiling BSP/violet..." -ForegroundColor Cyan
Compile "$ROOT/bsp/violet/board_violet.c" "$OUT/bsp/violet/board_violet.o"
Compile "$ROOT/bsp/violet/backlight.c" "$OUT/bsp/violet/backlight.o"
Compile "$ROOT/bsp/violet/touch_evdev.c" "$OUT/bsp/violet/touch_evdev.o"
Compile "$ROOT/bsp/violet/audio.c" "$OUT/bsp/violet/audio.o"
Compile "$ROOT/bsp/violet/wcn3990.c" "$OUT/bsp/violet/wcn3990.o"
Compile "$ROOT/bsp/violet/bluetooth_hci.c" "$OUT/bsp/violet/bluetooth_hci.o"
Compile "$ROOT/bsp/violet/power_supply.c" "$OUT/bsp/violet/power_supply.o"
Compile "$ROOT/bsp/violet/usb_gadget.c" "$OUT/bsp/violet/usb_gadget.o"
Compile "$ROOT/bsp/violet/usb_gadget_bind.c" "$OUT/bsp/violet/usb_gadget_bind.o"
Compile "$ROOT/bsp/violet/panel_drm.c" "$OUT/bsp/violet/panel_drm.o"
Compile "$ROOT/bsp/violet/display_orientation.c" "$OUT/bsp/violet/display_orientation.o"

# GUI
Write-Host "Compiling GUI..." -ForegroundColor Cyan
Compile "$ROOT/gui/gfx/gfx.c" "$OUT/gui/gfx/gfx.o"
Compile "$ROOT/gui/gfx/theme.c" "$OUT/gui/gfx/theme.o"
Compile "$ROOT/gui/gfx/font.c" "$OUT/gui/gfx/font.o"
Compile "$ROOT/gui/gfx/font_5x7.c" "$OUT/gui/gfx/font_5x7.o"
Compile "$ROOT/gui/gfx/anim.c" "$OUT/gui/gfx/anim.o"
Compile "$ROOT/gui/gfx/wallpaper.c" "$OUT/gui/gfx/wallpaper.o"
Compile "$ROOT/gui/compositor.c" "$OUT/gui/compositor.o"
Compile "$ROOT/gui/window_manager.c" "$OUT/gui/window_manager.o"
Compile "$ROOT/gui/wm_nav.c" "$OUT/gui/wm_nav.o"
Compile "$ROOT/gui/widgets/widgets.c" "$OUT/gui/widgets/widgets.o"
Compile "$ROOT/gui/widgets/button.c" "$OUT/gui/widgets/button.o"
Compile "$ROOT/gui/widgets/text_input.c" "$OUT/gui/widgets/text_input.o"
Compile "$ROOT/gui/widgets/widget_grid.c" "$OUT/gui/widgets/widget_grid.o"

# Apps
Write-Host "Compiling apps..." -ForegroundColor Cyan
Compile "$ROOT/apps/apps.c" "$OUT/apps/apps.o"
Compile "$ROOT/apps/launcher/home.c" "$OUT/apps/launcher/home.o"
Compile "$ROOT/apps/lock/lock_screen.c" "$OUT/apps/lock/lock_screen.o"
Compile "$ROOT/apps/settings/settings.c" "$OUT/apps/settings/settings.o"
Compile "$ROOT/apps/settings/wifiui.c" "$OUT/apps/settings/wifiui.o"
Compile "$ROOT/apps/settings/btui.c" "$OUT/apps/settings/btui.o"
Compile "$ROOT/apps/settings/features.c" "$OUT/apps/settings/features.o"
Compile "$ROOT/apps/settings/language.c" "$OUT/apps/settings/language.o"
Compile "$ROOT/apps/messages/messages.c" "$OUT/apps/messages/messages.o"
Compile "$ROOT/apps/gallery/gallery.c" "$OUT/apps/gallery/gallery.o"
Compile "$ROOT/apps/browser/browser.c" "$OUT/apps/browser/browser.o"
Compile "$ROOT/apps/bootui/bootui.c" "$OUT/apps/bootui/bootui.o"
Compile "$ROOT/apps/recovery/recovery.c" "$OUT/apps/recovery/recovery.o"
Compile "$ROOT/apps/fastboot/fastboot.c" "$OUT/apps/fastboot/fastboot.o"
Compile "$ROOT/apps/usbui/usbui.c" "$OUT/apps/usbui/usbui.o"
Compile "$ROOT/apps/keyboard/keyboard.c" "$OUT/apps/keyboard/keyboard.o"

# Filesystem
Write-Host "Compiling filesystem..." -ForegroundColor Cyan
Compile "$ROOT/fs/cpio.c" "$OUT/fs/cpio.o"
Compile "$ROOT/fs/gzip.c" "$OUT/fs/gzip.o"
Compile "$ROOT/fs/initramfs_gz.c" "$OUT/fs/initramfs_gz.o"
Compile "$ROOT/fs/fat16.c" "$OUT/fs/fat16.o"
Compile "$ROOT/fs/rescue.c" "$OUT/fs/rescue.o"
Compile "$ROOT/fs/storage.c" "$OUT/fs/storage.o"
Compile "$ROOT/fs/fstab.c" "$OUT/fs/fstab.o"
Compile "$ROOT/fs/rootfs.c" "$OUT/fs/rootfs.o"
Compile "$ROOT/fs/mount.c" "$OUT/fs/mount.o"

# Libc stubs
Write-Host "Compiling libc stubs..." -ForegroundColor Cyan
Compile "$ROOT/libc_stubs.c" "$OUT/libc_stubs.o"

# Init (PID 1)
Write-Host "Compiling init..." -ForegroundColor Cyan
Compile "$ROOT/init/main.c" "$OUT/init/main.o"

# Link
Write-Host "Linking qeafinit.elf..." -ForegroundColor Cyan
$OBJS = @(
    "$OUT/core/log.o", "$OUT/core/event_queue.o", "$OUT/core/scheduler/scheduler.o",
    "$OUT/core/memory/heap.o", "$OUT/core/sysio.o", "$OUT/core/platform.o",
    "$OUT/core/clock.o", "$OUT/core/i18n.o", "$OUT/core/prefs.o",
    "$OUT/hal/src/hal_manager.o",
    "$OUT/bsp/violet/board_violet.o", "$OUT/bsp/violet/backlight.o",
    "$OUT/bsp/violet/touch_evdev.o", "$OUT/bsp/violet/audio.o",
    "$OUT/bsp/violet/wcn3990.o", "$OUT/bsp/violet/bluetooth_hci.o",
    "$OUT/bsp/violet/power_supply.o",
    "$OUT/bsp/violet/usb_gadget.o",
    "$OUT/bsp/violet/usb_gadget_bind.o",
    "$OUT/bsp/violet/panel_drm.o",
    "$OUT/bsp/violet/display_orientation.o",
    "$OUT/gui/gfx/gfx.o", "$OUT/gui/gfx/theme.o", "$OUT/gui/gfx/font.o", "$OUT/gui/gfx/font_5x7.o",
    "$OUT/gui/gfx/anim.o", "$OUT/gui/gfx/wallpaper.o", "$OUT/gui/compositor.o",
    "$OUT/gui/window_manager.o", "$OUT/gui/wm_nav.o",
    "$OUT/gui/widgets/widgets.o", "$OUT/gui/widgets/button.o", "$OUT/gui/widgets/text_input.o",
    "$OUT/gui/widgets/widget_grid.o",
    "$OUT/apps/apps.o", "$OUT/apps/launcher/home.o", "$OUT/apps/lock/lock_screen.o",
    "$OUT/apps/settings/settings.o", "$OUT/apps/settings/wifiui.o",
    "$OUT/apps/settings/btui.o", "$OUT/apps/settings/features.o",
    "$OUT/apps/settings/language.o", "$OUT/apps/messages/messages.o",
    "$OUT/apps/gallery/gallery.o", "$OUT/apps/browser/browser.o",
    "$OUT/apps/bootui/bootui.o", "$OUT/apps/recovery/recovery.o",
    "$OUT/apps/fastboot/fastboot.o",
    "$OUT/apps/usbui/usbui.o",
    "$OUT/apps/keyboard/keyboard.o",
    "$OUT/fs/cpio.o", "$OUT/fs/gzip.o", "$OUT/fs/initramfs_gz.o",
    "$OUT/fs/fat16.o", "$OUT/fs/rescue.o",
    "$OUT/fs/storage.o",
    "$OUT/fs/fstab.o", "$OUT/fs/rootfs.o", "$OUT/fs/mount.o",
    "$OUT/libc_stubs.o",
    "$OUT/init/main.o"
)
& $ZIG cc -target aarch64-linux-musl -ffreestanding -nostdlib -fno-builtin -nostdlib -e main $OBJS -o "$OUT/qeafinit.elf"
if ($LASTEXITCODE -ne 0) { Write-Error "Linking failed" }

# Boot image gate -- never wrap qeafinit.elf in an ANDROID! header.
# The old pack step produced a 1 MB image with page_size=1024, ramdisk_size=0
# and an ELF where the bootloader expects a gzip kernel. Its 97 KB sibling was
# flashed to p77 and verified byte-for-byte, then the phone bootlooped.
$BOOTIMG = Join-Path $OUT "boot-qeaf-violet.img"
$REJECTED = Join-Path $OUT "rejected"

if (Test-Path $BOOTIMG) {
    python "$ROOT/scripts/validate_bootimg.py" --quiet $BOOTIMG 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) {
        New-Item -ItemType Directory -Path $REJECTED -Force | Out-Null
        $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
        $dest = Join-Path $REJECTED "boot-qeaf-violet.$stamp.img.bad"
        Move-Item -Path $BOOTIMG -Destination $dest -Force
        Write-Warning "Quarantined a non-bootable image: $BOOTIMG -> $dest"
        Write-Warning "Reason: ELF kernel payload / page_size 1024 / empty ramdisk. Never copy it to QEAFRESCUE as BOOTIMG.BIN."
    }
}

if ($P77Image -ne "") {
    Write-Host "Staging validated p77 image: $P77Image" -ForegroundColor Cyan
    python "$ROOT/scripts/pack_bootimg.py" $P77Image $BOOTIMG 2>&1
    if ($LASTEXITCODE -ne 0) { Write-Error "Refusing to stage an invalid p77 image"; exit 1 }
    python "$ROOT/scripts/validate_bootimg.py" $BOOTIMG 2>&1
    if ($LASTEXITCODE -ne 0) { Write-Error "Staged image failed validation"; exit 1 }
    Write-Host "  Boot image: $BOOTIMG (validated)" -ForegroundColor Green
} else {
    Write-Host "No boot image produced (by design)." -ForegroundColor Yellow
    Write-Host "  qeafinit.elf is a userspace ELF, not a Linux kernel." -ForegroundColor Yellow
    Write-Host "  To stage a real one: pwsh build\build.ps1 -P77Image out\boot-qeaf-violet-m48-noandroid.img" -ForegroundColor Yellow
    Write-Host "  To put it on the rescue LUN: python scripts\prepare_rescue_flash.py --volume F:\ --commit" -ForegroundColor Yellow
}

# The validator above legitimately exits 1 for a quarantined image. That is a
# successful outcome for this build, so do not let its exit code leak out and
# make a clean compile+link look like a failure to the caller.
$global:LASTEXITCODE = 0
Write-Host "Build complete (compile + link only; p77 untouched)." -ForegroundColor Green
Write-Host "  Kernel ELF: $OUT/qeafinit.elf" -ForegroundColor Green
