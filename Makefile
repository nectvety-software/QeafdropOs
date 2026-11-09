# SPDX-License-Identifier: MIT
# QeafdropOs - one OS for Xiaomi Redmi Note 7 Pro (violet), ARM aarch64.
#
# The single shipping OS is the Homo-derived LVGL desktop vendored under homo/,
# linked with the shared rescue channel (fs/, core/) into one static PID 1.
# One tree, one image, one version (qeaf_version.h):
#
#   make os           build + package + validate out/boot-qeafdropos-violet.img
#                     (this is THE OS; flash it to p77, never p76/boot)
#
# The targets below are LEGACY UMOS: qeafinit.elf and the RGB565 shell gates.
# They are kept as host fixtures for the shared rescue/storage channel and as
# historical UI regression; they are NOT compiled into the shipping image.
#
#   make all          legacy: cross-compile qeafinit + HAL + BSP (userspace ELF)
#   make bootimg      legacy: stage a VALIDATED p77 image (P77_IMAGE=...)
#   make check        legacy: compile check with the device flags
#   make guard-test   regression gates: bootloop guard + no-Android GUI/image
#   make rescue-channel-check / storage-check   the SHARED fs/ channel gates
#             (these validate the code the shipping OS also links)
#   make bootui-render / wifi-check / bt-check / usb-check / features-check /
#        keyboard-check / power-check / motion-check   legacy UMOS shell screens
#   make clean
#
# qeafinit.elf is a userspace ELF. Wrapping it in an ANDROID! header produces a
# file the rescue OS accepts (it only checks the magic) but the bootloader
# cannot execute -- that was the 2026-11-04 p77 bootloop. Never do it.

# Bare `zig` is not on PATH on this host; default to the bundled toolchain the
# host gates use, overridable with make ZIG=...
ZIG ?= out/toolchain/zig-windows-x86_64-0.13.0/zig.exe
TARGET := aarch64-linux-musl
CC := $(ZIG) cc -target $(TARGET) -ffreestanding -nostdlib -fno-builtin \
      -fno-stack-protector -nostdinc -O2 -Wall -Wextra

ROOT := .
OUT := out
KERNEL_ELF := $(OUT)/qeafinit.elf
BOOT_IMG := $(OUT)/boot-qeaf-violet.img

CORE_SRCS := core/log.c \
             core/event_queue.c \
             core/scheduler/scheduler.c \
             core/memory/heap.c \
             core/sysio.c \
             core/clock.c \
             core/i18n.c \
             core/prefs.c \
             core/platform.c

HAL_SRCS := hal/src/hal_manager.c

BSP_SRCS := bsp/violet/board_violet.c \
            bsp/violet/backlight.c \
            bsp/violet/touch_evdev.c \
            bsp/violet/audio.c \
            bsp/violet/wcn3990.c \
            bsp/violet/power_supply.c \
            bsp/violet/display_orientation.c \
            bsp/violet/usb_gadget.c \
            bsp/violet/usb_gadget_bind.c \
            bsp/violet/panel_drm.c

INIT_SRCS := init/main.c

SRCS := $(CORE_SRCS) $(HAL_SRCS) $(BSP_SRCS) $(INIT_SRCS)

INCLUDES := -I. \
            -Ihal/include \
            -Icore \
            -Icore/scheduler \
            -Icore/memory \
            -Ibsp/violet \
            -Igui/gfx \
            -Igui/widgets \
            -Iapps \
            -Iinit

OBJS := $(patsubst %.c,$(OUT)/%.o,$(SRCS))

.PHONY: os all bootimg check guard-test bootui-render wifi-check bt-check usb-check rescue-channel-check keyboard-check features-check violet-core-check power-check motion-check storage-check language-check prefs-test prefs-boot-test homo-i18n-test i18n-catalog-check vi-font-check ios-metrics-check display-default-check clean

HOST_ZIG ?= out/toolchain/zig-windows-x86_64-0.13.0/zig.exe
# zig spawned by make cannot reach the Windows AppData cache and dies
# AppDataDirUnavailable before reading a source file; pin it into out/.
export ZIG_GLOBAL_CACHE_DIR := $(CURDIR)/out/zig-global
BOOTUI_SRCS := tests/recovery_gui_test.c \
               apps/bootui/bootui.c \
               apps/fastboot/fastboot.c \
               apps/recovery/recovery.c \
               gui/gfx/gfx.c gui/gfx/theme.c gui/gfx/wallpaper.c \
               gui/gfx/font.c gui/gfx/font_5x7.c

# Only a pre-built, validated p77 install image may be staged. Empty by default.
P77_IMAGE ?=

# THE single shipping OS: Homo-derived LVGL desktop + shared fs/ rescue channel,
# one static aarch64-musl PID 1, one validated p77 image, one version.
os: homo-i18n-test ios-metrics-check display-default-check
	python3 homo/scripts/build_violet.py
	python3 homo/scripts/package_violet.py
	python3 scripts/validate_bootimg.py $(OUT)/boot-qeafdropos-violet.img
	@echo "Single OS image: $(OUT)/boot-qeafdropos-violet.img (flash to p77)"

# LEGACY UMOS userspace ELF below; not the shipping OS. Kept for host gates.
all: $(KERNEL_ELF)

$(OUT):
	mkdir -p $(OUT)
	mkdir -p $(OUT)/core
	mkdir -p $(OUT)/core/scheduler
	mkdir -p $(OUT)/core/memory
	mkdir -p $(OUT)/hal/src
	mkdir -p $(OUT)/bsp/violet
	mkdir -p $(OUT)/init

$(OUT)/%.o: %.c | $(OUT)
	$(CC) $(INCLUDES) -c $< -o $@

$(KERNEL_ELF): $(OBJS) | $(OUT)
	$(CC) -Ttext 0x80000000 $(OBJS) -o $@
	@echo "Built: $@ ($(shell wc -c < $@) bytes)"

# Deliberately fail-closed. There is no way to turn qeafinit.elf into a
# bootable p77 image; a real one needs a gzip kernel + initramfs (the old
# mk-install-image.sh pipeline). See scripts/validate_bootimg.py.
bootimg: $(KERNEL_ELF)
ifeq ($(strip $(P77_IMAGE)),)
	@echo "REFUSED: qeafinit.elf is a userspace ELF, not a Linux kernel." >&2
	@echo "  Wrapping it in ANDROID! created the 2026-11-04 p77 bootloop." >&2
	@echo "  Use: make bootimg P77_IMAGE=out/boot-qeaf-violet-m48-noandroid.img" >&2
	@echo "  Then: python3 scripts/prepare_rescue_flash.py --volume <LUN> --commit" >&2
	@exit 3
else
	python3 scripts/validate_bootimg.py $(P77_IMAGE)
	python3 scripts/pack_bootimg.py $(P77_IMAGE) $(BOOT_IMG)
	python3 scripts/validate_bootimg.py $(BOOT_IMG)
	@echo "Boot image: $(BOOT_IMG) (validated)"
endif

# zig cc has no -fsyntax-only (it dies FileNotFound on any input), so the
# compile check builds the real objects with -Wall -Wextra instead.
check: $(OBJS)
	@echo "compile check ok: $(words $(OBJS)) objects"

guard-test:
	python3 tests/validate_bootimg_test.py
	python3 tests/gui_no_android_test.py
	python3 tests/strip_android_image_test.py
	python3 tests/i18n_catalog_test.py
	python3 -c "import json; json.load(open('atlas.json', encoding='utf-8'))"

# Real RGB565 framebuffer, same draw() functions the phone calls, 54 assertions.
bootui-render:
	$(HOST_ZIG) cc -O1 -Wall -Wextra -I. -Ihal/include -Igui/gfx \
		-Igui/widgets -Iapps $(BOOTUI_SRCS) -o out/recovery_gui_test.exe
	./out/recovery_gui_test.exe out/bootui_fastboot.raw \
		out/bootui_recovery.raw out/bootui_confirm.raw
	python3 tests/recovery_gui_png.py out/bootui_fastboot.raw out/bootui_fastboot 1080 2340
	python3 tests/recovery_gui_png.py out/bootui_recovery.raw out/bootui_recovery 1080 2340
	python3 tests/recovery_gui_png.py out/bootui_confirm.raw out/bootui_confirm 1080 2340

# The Wi-Fi settings screens against the same radio seam the phone uses:
# the off frame is the in-run negative control for the on frame.
WIFI_SRCS := tests/wifi_gui_test.c apps/settings/wifiui.c \
             apps/bootui/bootui.c bsp/violet/wcn3990.c core/sysio.c \
             gui/gfx/gfx.c gui/gfx/theme.c gui/gfx/wallpaper.c \
             gui/gfx/font.c gui/gfx/font_5x7.c

wifi-check:
	$(HOST_ZIG) cc -O1 -Wall -Wextra -I. -Ihal/include -Igui/gfx \
		-Igui/widgets -Iapps $(WIFI_SRCS) -o out/wifi_gui_test.exe
	./out/wifi_gui_test.exe out/wifi_list_off.raw \
		out/wifi_list_on.raw out/wifi_details.raw
	python3 tests/recovery_gui_png.py out/wifi_list_off.raw out/wifi_list_off 1080 2340
	python3 tests/recovery_gui_png.py out/wifi_list_on.raw out/wifi_list_on 1080 2340
	python3 tests/recovery_gui_png.py out/wifi_details.raw out/wifi_details 1080 2340

# "Use USB for" sheet + the real gadget mode seam. The FAT16/LUN half is
# stubbed in the harness; everything else is the code the phone links.
USB_SRCS := tests/usb_gui_test.c apps/usbui/usbui.c \
            apps/bootui/bootui.c bsp/violet/usb_gadget.c \
            bsp/violet/display_orientation.c \
            gui/gfx/gfx.c gui/gfx/theme.c gui/gfx/wallpaper.c \
            gui/gfx/font.c gui/gfx/font_5x7.c

usb-check:
	$(HOST_ZIG) cc -O1 -Wall -Wextra -I. -Ihal/include -Igui/gfx \
		-Igui/widgets -Iapps $(USB_SRCS) -o out/usb_gui_test.exe
	./out/usb_gui_test.exe
	python3 tests/recovery_gui_png.py out/usb_sheet_charge.raw out/usb_sheet_charge 1080 2340
	python3 tests/recovery_gui_png.py out/usb_sheet_ptp.raw out/usb_sheet_ptp 1080 2340

FEATURES_SRCS := tests/features_test.c apps/settings/features.c \
                 core/platform.c gui/widgets/widget_grid.c \
                 gui/window_manager.c apps/bootui/bootui.c \
                 bsp/violet/display_orientation.c core/log.c core/sysio.c \
                 gui/gfx/gfx.c gui/gfx/theme.c gui/gfx/font.c \
                 gui/gfx/font_5x7.c gui/gfx/wallpaper.c

features-check:
	$(HOST_ZIG) cc -O1 -Wall -Wextra -I. -Ihal/include -Icore -Igui/gfx \
		-Igui/widgets -Iapps $(FEATURES_SRCS) -o out/features_test.exe
	./out/features_test.exe
	python3 tests/recovery_gui_png.py out/features_page.raw out/features_page 1080 2340
	python3 tests/recovery_gui_png.py out/features_split.raw out/features_split 1080 2340

KEYBOARD_SRCS := tests/keyboard_gui_test.c apps/keyboard/keyboard.c \
                 apps/bootui/bootui.c bsp/violet/display_orientation.c \
                 core/log.c gui/gfx/gfx.c gui/gfx/theme.c \
                 gui/gfx/wallpaper.c gui/gfx/font.c gui/gfx/font_5x7.c

keyboard-check:
	$(HOST_ZIG) cc -O1 -Wall -Wextra -I. -Ihal/include -Igui/gfx \
		-Igui/widgets -Iapps $(KEYBOARD_SRCS) -o out/keyboard_gui_test.exe
	./out/keyboard_gui_test.exe
	python3 tests/recovery_gui_png.py out/keyboard_portrait.raw out/keyboard_portrait 1080 2340
	python3 tests/recovery_gui_png.py out/keyboard_landscape.raw out/keyboard_landscape 1080 2340

RESCUE_SRCS := tests/rescue_channel_test.c fs/fat16.c fs/storage.c \
               fs/rescue.c core/sysio.c core/log.c bsp/violet/usb_gadget.c \
               bsp/violet/display_orientation.c gui/gfx/gfx.c

rescue-channel-check:
	mkdir -p out/rescue_fx
	$(HOST_ZIG) cc -O1 -Wall -Wextra -I. -Ihal/include -Icore -Igui/gfx \
		-Iapps $(RESCUE_SRCS) -o out/rescue_channel_test.exe
	./out/rescue_channel_test.exe

# Bluetooth: same inquiry seam the phone uses, plus the settings hub that
# reaches both radio screens. The off / no-adapter frames are in-run negative
# controls for the populated roster.
BT_SRCS := tests/bt_gui_test.c apps/settings/settings.c \
           apps/settings/wifiui.c apps/settings/btui.c \
           apps/settings/features.c apps/settings/language.c \
           core/i18n.c core/clock.c \
           apps/keyboard/keyboard.c apps/bootui/bootui.c \
           bsp/violet/bluetooth_hci.c bsp/violet/display_orientation.c \
           bsp/violet/wcn3990.c core/sysio.c core/platform.c \
           gui/gfx/gfx.c gui/gfx/theme.c gui/gfx/wallpaper.c \
           gui/gfx/font.c gui/gfx/font_5x7.c \
           gui/widgets/widget_grid.c

bt-check:
	$(HOST_ZIG) cc -O1 -Wall -Wextra -I. -Ihal/include -Igui/gfx \
		-Igui/widgets -Iapps $(BT_SRCS) -o out/bt_gui_test.exe
	./out/bt_gui_test.exe out/bt_list_off.raw \
		out/bt_list_on.raw out/bt_details.raw
	python3 tests/recovery_gui_png.py out/bt_list_off.raw out/bt_list_off 1080 2340
	python3 tests/recovery_gui_png.py out/bt_list_on.raw out/bt_list_on 1080 2340
	python3 tests/recovery_gui_png.py out/bt_details.raw out/bt_details 1080 2340
	python3 tests/recovery_gui_png.py out/settings_hub.raw out/settings_hub 1080 2340

# The charge line on the lock screen, fed by a fixture power_supply tree:
# the unplugged frame is the in-run negative control for the charging frame.
POWER_SRCS := tests/power_gui_test.c apps/lock/lock_screen.c \
              apps/bootui/bootui.c bsp/violet/power_supply.c core/sysio.c \
              core/clock.c core/i18n.c \
              gui/gfx/gfx.c gui/gfx/theme.c gui/gfx/wallpaper.c \
              gui/gfx/font.c gui/gfx/font_5x7.c

# The Language sheet + clock: portrait and landscape framebuffers, real taps.
LANGUAGE_SRCS := tests/language_gui_test.c apps/settings/language.c \
                 apps/bootui/bootui.c core/i18n.c core/clock.c \
                 core/sysio.c core/log.c gui/gfx/gfx.c \
                 gui/gfx/font.c gui/gfx/font_5x7.c

language-check:
	$(HOST_ZIG) cc -O1 -Wall -Wextra -I. -Ihal/include -Icore -Igui/gfx \
		-Igui/widgets -Iapps $(LANGUAGE_SRCS) -o out/language_gui_test.exe
	./out/language_gui_test.exe

# Preferences in /etc/qeaf.conf: parse, round-trip and change-only writes.
PREFS_SRCS := tests/prefs_test.c core/prefs.c core/sysio.c core/log.c

prefs-test:
	$(HOST_ZIG) cc -O1 -Wall -Wextra -I. -Icore $(PREFS_SRCS) -o out/prefs_test.exe
	./out/prefs_test.exe

# Two processes: `write` saves into a p77-tail fixture, `verify` removes the
# volatile /etc copy and loads the record back - the host's reboot analogue.
PREFS_BOOT_SRCS := tests/prefs_boot_test.c core/prefs.c core/sysio.c core/log.c

prefs-boot-test:
	$(HOST_ZIG) cc -O1 -Wall -Wextra -I. -Icore $(PREFS_BOOT_SRCS) \
		-o out/prefs_boot_test.exe
	./out/prefs_boot_test.exe write out/prefs_boot.img out/prefs_boot.conf
	./out/prefs_boot_test.exe verify out/prefs_boot.img out/prefs_boot.conf

# The phone ships portrait unless a stored record says otherwise. A silent
# revert of the default is invisible on the host desktop, so pin it here; the
# setter round-trip in the test is the negative control.
display-default-check: | $(OUT)
	$(HOST_ZIG) cc -O1 -Wall -Wextra -I. -Ihal/include -Ihomo/platform/violet \
		tests/display_default_test.c homo/platform/violet/display_hal.c \
		-o out/display_default_test.exe
	./out/display_default_test.exe

# Catalog regression for the Homo label hook: Chinese fallback, EN/VI copy,
# title text and the app-name table. Runs before `make os`.  The stub supplies
# only the LVGL setter that i18n.c calls.
homo-i18n-test: i18n-catalog-check vi-font-check | $(OUT)
	$(HOST_ZIG) cc -O1 -Wall -Wextra -I. -Ihomo/framework/ui \
		-Itests/homo_i18n_stub tests/homo_i18n_test.c \
		homo/framework/ui/i18n.c -o out/homo_i18n_test.exe
	./out/homo_i18n_test.exe

# Whole-tree catalog gate: every CJK literal in the UI sources must have an
# EN/VI row, translations must keep their printf specifiers, the scanner must
# flag its own planted probe, and launcher titles must resolve to text rather
# than a baked PNG. A missing row stays Chinese on the glass and every host
# render gate still passes, so this is the only place it can be caught.
i18n-catalog-check:
	python3 tests/i18n_catalog_test.py

# The catalog now stores Vietnamese with diacritics and the UI face is
# Liberation Sans with PingFang chained behind it. tiny_ttf reports a missing
# glyph as "not found", which renders as a box on the glass while every host
# render gate stays green - so the shipped cmap is measured against every
# catalog character, with a CJK negative control in the same run.
vi-font-check:
	python3 tests/vi_font_coverage_test.py

# Portrait Home geometry, pinned at the shipping 540x1170 canvas and at the
# untouched 800x480 landscape legacy grid. ios_metrics.c has no LVGL
# dependency on purpose, so the same fractions the desktop layout reads are
# measurable here without a display.
ios-metrics-check: | $(OUT)
	$(HOST_ZIG) cc -O1 -Wall -Wextra -Ihomo/framework/ui \
		tests/ios_metrics_test.c homo/framework/ui/ios_metrics.c \
		-o out/ios_metrics_test.exe
	./out/ios_metrics_test.exe

power-check:
	$(HOST_ZIG) cc -O1 -Wall -Wextra -I. -Ihal/include -Igui/gfx \
		-Igui/widgets -Iapps $(POWER_SRCS) -o out/power_gui_test.exe
	./out/power_gui_test.exe out/lock_unplugged.raw out/lock_charging.raw
	python3 tests/recovery_gui_png.py out/lock_unplugged.raw out/lock_unplugged 1080 2340
	python3 tests/recovery_gui_png.py out/lock_charging.raw out/lock_charging 1080 2340

# The wallpaper transition: the real compositor and blitter over a real
# 1080x2340 RGB565 panel, stepped 16 ms at a time. The frozen backdrop under an
# opaque window and the plate over a flat poster are the in-run negative
# controls for the ramp.
MOTION_SRCS := tests/motion_gui_test.c gui/compositor.c gui/window_manager.c \
               gui/gfx/anim.c gui/gfx/wallpaper.c gui/gfx/gfx.c gui/gfx/theme.c \
               gui/gfx/font.c gui/gfx/font_5x7.c \
               apps/bootui/bootui.c apps/launcher/home.c apps/lock/lock_screen.c \
               bsp/violet/power_supply.c core/sysio.c core/clock.c core/i18n.c

motion-check:
	$(HOST_ZIG) cc -O1 -Wall -Wextra -I. -Ihal/include -Igui/gfx \
		-Igui/widgets -Iapps $(MOTION_SRCS) -o out/motion_gui_test.exe
	./out/motion_gui_test.exe
	python3 tests/recovery_gui_png.py out/motion_home.raw out/motion_home 1080 2340
	python3 tests/recovery_gui_png.py out/motion_lock.raw out/motion_lock 1080 2340
	python3 tests/recovery_gui_png.py out/motion_sheet.raw out/motion_sheet 1080 2340
	python3 tests/recovery_gui_png.py out/motion_opaque.raw out/motion_opaque 1080 2340

STORAGE_SRCS := tests/storage_fat16_test.c fs/fat16.c fs/storage.c core/log.c

# The QEAFRESCUE volume as raw bytes: built by the device's own fat16.c, then
# walked back from the sectors by a second reader written from the FAT spec,
# and again by tests/check_rescue_volume.py - an independent implementation.
storage-check:
	@mkdir -p $(OUT)/t
	$(HOST_ZIG) cc -O1 -Wall -Wextra -I. -Ihal/include -Igui/gfx \
		-Igui/widgets -Iapps $(STORAGE_SRCS) -o out/storage_fat16_test.exe
	./out/storage_fat16_test.exe > out/t/gate-storage.log 2>&1
	python3 tests/check_rescue_volume.py out/qeafrescue_meta.img \
		>> out/t/gate-storage.log 2>&1
	@cat out/t/gate-storage.log

# The current OS core: Homo OS / LVGL ported to violet, with a REAL DRM/KMS
# panel driver and a REAL configfs f_mass_storage LUN (homo/platform/violet).
# This gate is the whole reason the p77 image no longer boots to a blank
# screen: it proves the transform maths, the image structure, and that /init
# itself carries both hardware drivers.
VIOLET_CORE_IMG := $(OUT)/boot-qeafdropos-violet.img

violet-core-check:
	$(HOST_ZIG) cc -O1 -Wall -Wextra -Ihomo -Ihomo/platform/violet \
		homo/tests/panel_scale_test.c homo/platform/violet/panel_scale.c \
		-o $(OUT)/panel_scale_test.exe
	./$(OUT)/panel_scale_test.exe | tail -4
	python3 scripts/validate_bootimg.py $(VIOLET_CORE_IMG)
	python3 scripts/audit_rescue_channel.py $(VIOLET_CORE_IMG)
	python3 -c "import sys; d=open('homo/build/violet/homo-init','rb').read(); \
		sys.exit(0 if b'QeafdropOs 0.1.0' in d else 1)" && \
		echo "ok   homo-init carries the single QeafdropOs 0.1.0 identity"

clean:
	rm -rf $(OUT)
