# Homo OS on the Redmi Note 7 Pro (`violet`)

Homo OS normally targets an 800x480 i.MX6ULL board with a framebuffer and
devtmpfs. `violet` is the opposite kind of machine: a Xiaomi SDM660 phone that
boots a stock Android kernel from an initramfs that *is* the root filesystem.
This directory is the platform layer that bridges the two.

The boot image is built from QeafdropOs's proven pipeline (real violet kernel +
`newc`/gzip ramdisk + p77 header patcher) but the userspace is Homo OS:
`homo-init` is PID 1, owns the panel and runs the LVGL desktop instead of the
VQEAF boot UI.

## Hardware and kernel constraints

| Fact | Consequence here |
|---|---|
| Panel is 1080x2340 portrait, DSI, MSM DRM | `drm_kms.c` drives it with DRM ioctls directly |
| `# CONFIG_DRM_FBDEV_EMULATION is not set` | LVGL's fbdev driver cannot be used; there is no `/dev/fb0` |
| `# CONFIG_DEVTMPFS is not set` | `devnodes.c` creates every `/dev` node from `/sys/class/*/dev` |
| `CONFIG_DRM_MSM=y`, `CONFIG_DRM_MSM_DSI_STAGING=y` | MSM is the only scanout path; a dumb buffer + `ADDFB`/`SETCRTC` is enough |
| `CONFIG_INPUT_EVDEV=y` | touch is read as raw `struct input_event` in `violet_input.c` |
| `CONFIG_CONFIGFS_FS=y`, `CONFIG_USB_CONFIGFS_MASS_STORAGE=y`, `CONFIG_USB_F_MASS_STORAGE=y` | the QEAFRESCUE drive is a real kernel USB gadget |
| `CONFIG_RD_GZIP`, no LZ4 | the ramdisk must be gzip |
| No framebuffer console | logs go to `stderr` (the kernel's `/dev/console` fd) and `/dev/kmsg` |

The shell follows the panel and portrait is the shipped default: its canvas is
540x960 (9:16) and landscape is 960x540 (16:9). Both are 2.0x fits (1080x1920
centred on 1080x2340, 1920x1080 centred on 2340x1080) with 210 px bars on the
long axis instead of a stretch. App modules are still drawn for 800x480, so
while one is open the platform switches the LVGL resolution to that canvas
(2.25x to 1800x1080, centred on the 2340x1080 logical plane with 270 px black
bars) and switches back to the shell when the app closes. Rotation 90/270 lays
the same mapping on its side. Everything is integer Q16 arithmetic in
`panel_scale.c`.

The DRM dumb buffer is **not** tightly packed: a 1080-pixel row has a 4352-byte
pitch (1088 pixels), so the scaler is told the real stride with
`vio_panel_set_stride()`. Rendering with `panel_w` as the stride sheared every
row by 8 pixels on the glass; the `snap` verb caught it as diagonal streaks.

## Files

| File | Role |
|---|---|
| `main_violet.c` | PID 1: mounts, backlight, LUN, DRM, LVGL desktop, main loop |
| `devnodes.c` | mounts (`proc`, `sysfs`, tmpfs, configfs), sysfs-driven `/dev`, logging, backlight, touch discovery |
| `drm_kms.c` | minimal DRM/KMS: `GETRESOURCES`/`GETCONNECTOR`/`CREATE_DUMB`/`ADDFB`/`SETCRTC`, optional `DIRTYFB` |
| `panel_scale.c` | UI canvas (540x960 / 960x540 shell, 800x480 app) -> panel scale, letterbox, rotation, touch inverse map |
| `main_violet.c` | PID 1 main loop; `ui_sync_canvas()` swaps the canvas when a rescue `rotate` or an app launch asks for it |
| `violet_input.c` | evdev multitouch reader, slot 0 only |
| `usb_lun.c` | 32 MiB FAT16 volume in an mmap'd tmpfs file, served over configfs `f_mass_storage`; also implements the `usb_gadget_*` HAL that `fs/rescue.c` calls |
| `display_hal.c` | the `hal/include/display.h` orientation seam used by `QEAF.CMD rotate` |
| `lv_conf.h` | LVGL config for this target (fbdev/DRM/evdev drivers disabled) |

`fs/fat16.c`, `fs/storage.c`, `fs/rescue.c`, `core/sysio.c`, `core/log.c`,
`core/prefs.c` and `core/i18n.c` are compiled straight from
`D:\QeafivelsOfficial\QeafdropOs`, so the rescue channel and the p77 preference
slot on the drive are the ones already tested on this phone.

## Boot sequence

1. Kernel unpacks the gzip initramfs as rootfs and execs `/init`.
2. `vio_setup_environment()` mounts `proc`, `sysfs`, tmpfs on `/dev`, `/run`,
   `/tmp` and configfs on `/config`, then builds `/dev` from sysfs.
2b. `load_session_prefs()` enables the CRC-checked `QEAFPRF1` record at the tail
   of p77, then applies language, orientation and auto-rotate before the panel
   binds. `/etc/qeaf.conf` remains the session copy; a corrupt or blank slot
   falls back to it and then to the built-in defaults.
3. Backlight is set to 85% through `/sys/class/backlight` or `/sys/class/leds`.
4. `vio_lun_init()` creates `/run/qeaf-lun.img` (32 MiB, `MAP_SHARED`),
   formats FAT16 with the five storage folders, publishes `HELP.TXT`,
   `SUMMARY.TXT`, `RESPONSE.TXT`, `QEAFBOOT.LOG` and `DEVINFO.TXT`.
5. `vio_lun_start_gadget()` builds `/config/usb_gadget/homo` and binds the
   first UDC, so a PC sees `QEAFRESCUE`.
6. `vio_drm_init()` finds the connected DSI connector, picks the mode the
   bootloader already programmed, allocates a dumb XRGB8888 buffer, adds an FB
   and sets the CRTC.
7. LVGL renders the desktop into `ui_buffer` (DIRECT mode, partial flushes);
   `flush_cb` scales/rotates only the invalidated rectangle into the scanout
   buffer.
8. Every 250 ms `rescue_poll()` reads `QEAF.CMD` from the volume and `display_poll()`
   applies auto-rotation; `save_session_prefs()` then writes a changed
   orientation/language to the p77 slot (read back byte for byte before it is
   believed). If DRM fails, the process stays alive and keeps the rescue drive
   working instead of panicking the kernel.

Fatal signals (`SIGSEGV`, `SIGBUS`, `SIGILL`, `SIGFPE`, `SIGABRT`) raised while
LVGL is running are caught, logged and longjmp'd out of: the desktop stops but
PID 1 keeps the LUN and the gadget alive. Faults outside the UI are re-raised.

Because the kernel and our init share the same page cache for
`/run/qeaf-lun.img`, a file the PC writes to the drive is immediately visible
to `rescue.c`; no sync step and no second copy of the volume exist.

## Build

```powershell
python scripts/build_violet.py     # cross-compile + link build/violet/homo-init
python scripts/package_violet.py   # stage, cpio+gzip, boot image
```

`build_violet.py` uses QeafdropOs's Zig 0.13.0 toolchain (override with `ZIG=`)
and the QeafdropOs checkout at `D:\QeafivelsOfficial\QeafdropOs` (override with
`QEAFDROPOS=`). It compiles LVGL, the Homo OS runtime, this platform layer and
the QeafdropOs rescue files for `aarch64-linux-musl` and links one static
binary. There is no `make` on Windows, and the link uses a response file because
510 object paths exceed the 32 KiB command line.

Current budget (measured on this tree):

```
staged initramfs   19 687 971 B
cpio               19 700 596 B
gzip ramdisk       10 625 115 B
boot image         28 139 520 B   (42% of p77's 64 MiB)
```

`homo-init` is 7 947 024 B; the boot image sha256 is
`4cc6571c2a81661b844e15974661edbef973ae993709c9dbcf06924f1bda5395`.

`custom/assets/maps` (97 MiB of Beijing tiles) is excluded by default; it cannot
fit in p77. `--with-maps` exists for experiments and will trip the size guard.

## Flash

The image goes to **p77 (recovery)**, exactly like the QeafdropOs images. It is
never written to p76/boot.

```powershell
# from the QEAFRESCUE drive exposed by the currently running OS
python D:\QeafivelsOfficial\QeafdropOs\scripts\prepare_rescue_flash.py `
  --image D:\os-system\Homo-OS-Linux\build\violet\boot-homo-violet.img `
  --volume F:            # dry run: read the plan first
# add --commit to the same command to agree to the p77 write

# or, when nothing boots:
fastboot flash recovery build\violet\boot-homo-violet.img
fastboot reboot recovery
```

`mk_bootimg_violet.py` copies the stock header page and kernel blob byte for
byte and rewrites only `ramdisk_size`, `ramdisk_addr` and the cmdline, then
`validate_bootimg.py` checks page size, kernel gzip member, ramdisk gzip member,
addresses and the 64 MiB partition limit. The stock cmdline is kept, including
`force_initramfs prefer_initramfs`.

## Host test

The scaler is a pure function, so it is tested without a phone:

```powershell
zig cc -std=c99 -Wall -Wextra -I. tests/panel_scale_test.c platform/violet/panel_scale.c -o build/panel_scale_test.exe
build\panel_scale_test.exe
```

It checks the portrait shell geometry (540x1170 at 2.0x, no bars, last sample touches the panel corner), the app-canvas switch to 1170x540 and back to the shell, all four rotations, that 90 tiled
partial flushes reproduce a full frame pixel for pixel, that letterbox pixels
stay black, that touch round-trips within one pixel and rejects taps in the
bars, and that a 1088-pixel DRM stride renders the same frame while leaving the
8-pixel row padding untouched. `ctest` runs it as `panel_scale_test` on Linux CI.

## What is verified, and what is not

Verified on the host:

- the whole userspace compiles and links as a static aarch64-musl ELF (no
  `PT_INTERP`), 7.9 MB;
- the cpio passes `mk_cpio.py --check` (executable `/init`, valid newc, trailer);
- the boot image passes `validate_bootimg.py` (kernel/ramdisk gzip members,
  addresses, page size, size <= p77);
- the scale/letterbox/rotation/touch maths in `tests/panel_scale_test.c`,
  including the 540x1170 portrait shell and the 1170x540 app-canvas switch;
- `tests/display_default_test.c` pins the shipped defaults: portrait, zero
  degrees, auto-rotate off, and the 540x1170 shell canvas;
- `tests/ios_metrics_test.c` pins the portrait Home grid at that canvas (41
  checks: 4 columns, 83 px icons, 44 px column gap, 38 px margins, 15 px labels,
  148 px row pitch, 114 px dock at y1028, 186x7 home bar at y1152, 46 px status
  strip) alongside the untouched 800x480 landscape numbers.

Verified on the phone (image sha256 `6ac214f9…`, 28,356,608 B, flashed to p77
2026-11-09 15:5x):

- both flash proofs are on disk: `out/report-raw-flash-homo/RESPONSE.TXT:2`
  `flash: written and verified byte for byte` and
  `out/report-raw-flash-homo/QEAFBOOT.LOG:5` `lun: verified - recovery and
  BOOTIMG.BIN agree byte for byte over 28356608 bytes`;
- DRM scanout: `out/report-raw/HOMOLOG.TXT:15` `drm: verified - crtc 107 scans
  our fb 188 at 1080x2340`, `:17` `scanout ready (continuous scanout)`;
- the desktop creates its canvas at 540x960 (`:20`), and the `snap` capture
  `out/report-raw/home-m50.png3x.png` shows the portrait Home: 4 columns of
  83 px icons, the dock slab and home bar, a crisp status bar, and labels in
  Vietnamese with diacritics -- the shell fills the panel, no black letterbox
  bands;
- the persisted language is Vietnamese: `:2` `prefs: language 0, orientation
  portrait` (language 0 = `I18N_VI`); the earlier A/B on the same boot lineage
  (`out/report-lang-vi2`, `out/report-lang-en2`) put the difference in the label
  bands only;
- `f_mass_storage` binds (`usb: mass storage bound to a600000.dwc3`), the host
  sees QEAFRESCUE, and the backlight is set to 85%.

Still unmeasured on the phone:

- a real-finger tap/gesture on this build: the channel has no `tap` verb, so the
  touch path is proved by the device opening and mapping `event1`, not by a
  finger;
- opening the Control Center: this build hides the Home status bar while it
  snapshots the blurred backdrop (so the sheet no longer sits under a smeared
  copy of its own clock), but the sheet only opens on a finger drag and the
  channel cannot inject one, so that fix is flashed and un-witnessed;
- frame rate. A full-screen scale is ~1.9 M pixels of integer work on one A53
  core; partial flushes only touch the invalidated rectangle, but the first
  frame and app transitions are the expensive ones.

## Known gaps

- **Wi-Fi / Bluetooth:** the QeafdropOs `wcn3990` and HCI drivers are not part
  of this build, and Homo OS's network page talks to helper binaries that do not
  exist in the initramfs. The Settings page renders; radio operations fail.
- **Audio:** `services/music.c` `dlopen`s `libasound.so.2`, which a static
  initramfs does not provide. The player UI works; playback reports no ALSA.
- **Offline maps:** tiles are excluded (size). The Maps app reports the missing
  directory instead of crashing.
- **Power:** `services/power.c` reads `/sys/class/power_supply`, so battery
  percentage works if the charger driver exposes it; there is no suspend,
  thermal policy or charging animation.
- **No shell, no second binary.** Recovery is the USB drive and `QEAF.CMD`.
- **Orientation default** is portrait: the shell canvas is 540x960 (9:16) and centres on the
  1080x2340 panel. `rotate 90|180|270` puts the UI on its side (the 800x480 app
  canvas becomes 1800x1080 with 270 px bars); the chosen value is persisted in
  the p77 slot and restored on the next boot. `auto` is off by default.
- **Language default** is Vietnamese (`I18N_VI`, the prefs value 0). The system
  shell, Settings, Control Center and launcher names have EN/VI copy in
  `framework/ui/i18n.c`; `lang vi|en|zh` in `QEAF.CMD` or the Language page
  rebuilds the desktop immediately and the choice is persisted in p77. App
  content without a catalog entry stays in its original Chinese copy.
