/* SPDX-License-Identifier: MIT */
/* init/main.c - qeafinit: PID 1 for UMOS/violet */

#include "hal/include/hal.h"
#include "hal/include/hal_board.h"
#include "core/log.h"
#include "core/event_queue.h"
#include "core/scheduler/scheduler.h"
#include "core/memory/heap.h"
#include "bsp/violet/board_violet.h"
#include "gui/gfx/gfx.h"
#include "gui/gfx/theme.h"
#include "gui/gfx/wallpaper.h"
#include "gui/gfx/font_5x7.h"
#include "gui/window_manager.h"
#include "gui/compositor.h"
#include "core/sysio.h"
#include "apps/apps.h"
#include "apps/recovery/recovery.h"
#include "apps/fastboot/fastboot.h"
#include "apps/settings/wifiui.h"
#include "apps/settings/btui.h"
#include "apps/usbui/usbui.h"
#include "hal/include/wifi.h"
#include "hal/include/bluetooth.h"
#include "fs/initramfs.h"
#include "fs/fstab.h"
#include "fs/rootfs.h"
#include "hal/include/usb_gadget.h"
#include "hal/include/display.h"
#include "core/platform.h"
#include "core/i18n.h"
#include "core/prefs.h"
#include "gui/widgets/widget_grid.h"

static void log_cstr(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    umos_log_raw(s, n);
}

/* The recovery GUI only collects intents. Executing them belongs to the
 * board / rescue shell; until that bridge is bound we record each request
 * verbatim instead of pretending the action already happened. */
static void drain_recovery_actions(void) {
    enum recovery_action a = recovery_take_action();
    if (a == RECOVERY_ACT_NONE) return;
    log_cstr("recovery: intent ");
    log_cstr(recovery_action_name(a));
    log_cstr("\n");
}

/* GUI-to-GUI navigation needs no privileged executor, so those two intents are
 * handled here. Everything else is only recorded as a request. */
static void drain_fastboot_actions(void) {
    enum fastboot_action a = fastboot_take_action();
    if (a == FASTBOOT_ACT_NONE) return;
    log_cstr("fastboot: intent ");
    log_cstr(fastboot_action_name(a));
    log_cstr("\n");
    if (a == FASTBOOT_ACT_RECOVERY) {
        fastboot_close();
        recovery_open();
    } else if (a == FASTBOOT_ACT_CONTINUE) {
        fastboot_close();
        wm_open(UMOS_APP_HOME);
    }
}

/* The Wi-Fi screen only queues association intents. The executor re-reads the
 * same scan the screen showed, asks the radio seam to associate, and leaves
 * every figure on the details page to wifi_link_info(). */
static void drain_wifi_actions(void) {
    int arg = 0;
    enum wifiui_action a = wifiui_take_action(&arg);
    if (a == WIFIUI_ACT_NONE) return;
    if (a == WIFIUI_ACT_CONNECT) {
        struct wifi_scan_result res[WIFI_MAX_SCAN_RESULTS];
        int n = wifi_scan(res, WIFI_MAX_SCAN_RESULTS);
        if (n < 0) n = 0;
        if (arg < 0 || arg >= n) {
            umos_logf("wifi: connect intent %d but the scan holds %d rows\n", arg, n);
            return;
        }
        struct wifi_config cfg;
        for (int i = 0; i < WIFI_MAX_SSID; i++) {
            cfg.ssid[i] = res[arg].ssid[i];
            if (!cfg.ssid[i]) break;
        }
        cfg.ssid[WIFI_MAX_SSID] = 0;
        cfg.key[0] = 0;
        cfg.auth = res[arg].auth;
        umos_logf("wifi: connect '%s' rc %d\n", cfg.ssid, wifi_connect(&cfg));
    } else if (a == WIFIUI_ACT_DISCONNECT) {
        umos_logf("wifi: disconnect rc %d\n", wifi_disconnect());
    } else {
        umos_logf("wifi: intent %d recorded, no executor bound\n", (int)a);
    }
}

/* The Bluetooth screen is the same contract: re-read the inquiry the user
 * tapped, then ask the HCI seam to pair/connect. Both refuse until a stack is
 * bound, and the log says so rather than claiming a bond was made. */
static void drain_bt_actions(void) {
    int arg = 0;
    enum btui_action a = btui_take_action(&arg);
    if (a == BTUI_ACT_NONE) return;
    if (a == BTUI_ACT_CONNECT || a == BTUI_ACT_PAIR ||
        a == BTUI_ACT_UNPAIR) {
        struct bt_device res[BT_MAX_DEVICES];
        int n = bt_scan(res, BT_MAX_DEVICES);
        int rc = -1;
        if (n > 0 && arg >= 0 && arg < n) {
            if (a == BTUI_ACT_CONNECT) rc = bt_connect(res[arg].addr);
            else if (a == BTUI_ACT_PAIR) rc = bt_pair(res[arg].addr);
            else rc = bt_unpair(res[arg].addr);
        } else {
            umos_logf("bt: intent %d but the inquiry holds %d rows\n", arg,
                      n < 0 ? 0 : n);
            return;
        }
        umos_logf("bt: %s '%s' rc %d\n",
                  a == BTUI_ACT_CONNECT ? "connect"
                  : a == BTUI_ACT_PAIR ? "pair" : "unpair",
                  res[arg].name, rc);
    } else if (a == BTUI_ACT_DISCONNECT) {
        umos_logf("bt: disconnect rc %d\n", bt_disconnect());
    } else {
        umos_logf("bt: intent %d recorded, no executor bound\n", (int)a);
    }
}

/* The USB sheet only queues a choice. This is the single place that applies
 * it to the gadget, so no other window can rebind USB behind the user's back. */
static void drain_usb_actions(void) {
    int arg = 0;
    enum usbui_action a = usbui_take_action(&arg);
    if (a == USBUI_ACT_NONE) return;
    if (a == USBUI_ACT_SET_MODE) {
        int rc = usb_gadget_set_mode((enum usb_mode)arg);
        umos_logf("usb: user chose %s rc %d\n",
                  usb_gadget_mode_name((enum usb_mode)arg), rc);
    } else {
        umos_logf("usb: notification dismissed, mode stays %s\n",
                  usb_gadget_mode_name(usb_gadget_mode()));
    }
    usbui_close();
}

/* Rising edge of VBUS only: re-plugging asks again, an already-plugged cable
 * does not reopen a sheet the user just dismissed. */
static void poll_usb_cable(void) {
    static int was_plugged = 0;
    int plugged = usb_gadget_cable();
    if (plugged && !was_plugged) {
        usbui_open();
        umos_logf("usb: PC cable detected, asking for a mode\n");
    } else if (!plugged && was_plugged) {
        usbui_close();
    }
    was_plugged = plugged;
}

static int qeaf_main(void) {
    void *arena = NULL;
    size_t arena_size = 0;
    /* Orientation to write back: with auto-rotate on, the sensor moves the
     * panel under the user, and a transient tilt is not a preference. */
    int pref_rot = (int)DISPLAY_PORTRAIT;

    /* 1. Board gives up its RAM */
    if (board_memory(&arena, &arena_size) != UMOS_OK || !arena || arena_size == 0) {
        umos_logf("boot: the board offers no heap; stopping here\n");
        return 1;
    }

    /* 2. Core heap */
    if (umos_heap_init(arena, arena_size) != UMOS_OK) {
        umos_logf("boot: the core heap refused the arena\n");
        return 1;
    }
    umos_logf("boot: heap %u bytes\n", (unsigned)umos_heap_total());

    /* 3. Violet board init (registers HAL drivers) */
    if (violet_board_init() != UMOS_OK) {
        umos_logf("boot: violet board init failed\n");
        return 1;
    }

    /* 4. HAL init */
    if (hal_init() != UMOS_OK) {
        umos_logf("boot: HAL init failed\n");
        return 1;
    }
    umos_logf("boot: HAL ready\n");

    /* 4. Graphics: bind framebuffer from HAL display */
    {
        uint32_t w, h, stride;
        struct hal_display *disp = hal_display();
        if (disp && disp->get_geometry && disp->fb &&
            disp->get_geometry(&w, &h, &stride) == UMOS_OK) {
            uint16_t *fb = disp->fb();
            if (fb) {
                gfx_init(fb, w, h, stride);
                theme_init();
                font_5x7_init();
                platform_apps_init();
                widget_grid_init();
                wallpaper_build();
                theme_apply_wallpaper();
                umos_logf("boot: display %lux%u stride=%u\n", w, h, stride);
                umos_logf("boot: dynamic palette %s\n",
                          theme_dynamic() ? "active" : "default");
            } else {
                umos_logf("boot: FATAL display - HAL offered no frame buffer; "
                          "the panel will stay blank\n");
            }
        } else {
            umos_logf("boot: FATAL display - no usable HAL display driver; "
                      "the panel will stay blank\n");
        }
    }

    /* 5. Radios + window manager + apps */
    wifi_init();
    bt_init();
    wm_init();
    compositor_init();
    umos_apps_register_all();
    umos_logf("boot: window manager + apps ready\n");

    /* 6. Event queue */
    umos_eventq_init();
    umos_logf("boot: event queue ready\n");

    /* 7. Scheduler */
    umos_logf("boot: scheduler ready (%d tasks)\n", umos_sched_tasks());

    /* 8. Filesystem: fstab + rootfs over initramfs */
    fstab_init();
    fstab_add("initramfs", "/", "initramfs", "ro");
    fstab_mount_all();
    rootfs_init("/");
    rootfs_mount_all();
    umos_logf("boot: fstab + rootfs mounted\n");

    /* 8b. Preferences. The durable copy is a CRC-checked record in the tail of
     * p77; /etc/qeaf.conf is the session copy. Both are read here, before any
     * shell screen draws, with the p77 slot taking precedence. */
    {
        int lang = (int)i18n_selected();
        int orient = (int)display_orientation();
        int auto_rotate = display_auto_rotate();
        prefs_set_block(PREFS_P77_PATH, PREFS_P77_SLOT_OFFSET);
        if (prefs_load(&lang, &orient, &auto_rotate) == 0) {
            if (lang >= 0 && lang < I18N_LANG_COUNT)
                i18n_set((enum i18n_lang)lang);
            if (orient >= 0 && orient < DISPLAY_ORIENTATION_COUNT)
                display_set_orientation((enum display_orientation)orient);
            display_set_auto_rotate(auto_rotate ? 1 : 0);
            umos_logf("prefs: language %d, orientation %s, auto %d (%s)\n",
                      (int)i18n_selected(),
                      display_orientation_name(display_orientation()),
                      display_auto_rotate(), prefs_source());
        } else {
            umos_logf("prefs: no readable %s; using defaults\n",
                      PREFS_DEFAULT_PATH);
        }
        pref_rot = (int)display_orientation();
    }

    /* 9. USB gadget: mass storage LUN + RNDIS/NCM */
    usb_gadget_init();
    usb_gadget_start();
    umos_logf("boot: usb gadget started\n");

    /* 9b. Bind that volume to the controller. usb_gadget_start() only registers
     * the LUN inside this process; an image without the configfs/UDC bind below
     * boots and enumerates NOTHING on USB - which is what every m48/m49/m49b
     * flash produced. It runs in a child: violet has no watchdog, so PID 1
     * parked in the gadget stack is a board recovered only by a power hold. */
    qeaf_gadget_start_detached();

    /* 10. Boot-mode GUI. p77 opens recovery, a fastboot command opens the
     * fastboot menu, and a normal boot opens the lock screen. */
    if (board_boot_recovery()) {
        recovery_open();
        umos_logf("boot: recovery GUI opened (p77)\n");
    } else if (board_boot_fastboot()) {
        fastboot_open();
        umos_logf("boot: fastboot GUI opened\n");
    } else {
        wm_open(UMOS_APP_LOCK);
    }
    /* The boot screen is the first thing the panel shows, so it arrives
     * settled: no ramp out of a home view the user never saw. */
    compositor_settle();

    umos_logf("boot: qeafinit (PID 1) ready\n");

    /* 11. Main loop */
    while (!board_should_exit()) {
        board_poll();
        recovery_poll_input();
        fastboot_poll_input();
        wm_dispatch();
        /* Every HAL callback on this board is a NULL stub. Guard the clock:
         * calling hal_rtc()->now_ms() here used to jump to address 0 on the
         * FIRST loop iteration and kill PID 1 before a frame was ever drawn. */
        {
            struct hal_rtc *rtc = hal_rtc();
            uint64_t now_ms = (rtc && rtc->now_ms)
                              ? (uint64_t)rtc->now_ms()
                              : sysio_monotonic_ms();
            umos_sched_tick(now_ms);
        }
        compositor_frame(sysio_monotonic_ms());
        wm_frame_multitask();
        drain_recovery_actions();
        drain_fastboot_actions();
        drain_wifi_actions();
        drain_bt_actions();
        drain_usb_actions();
        poll_usb_cable();
        display_poll();
        if (!display_auto_rotate()) pref_rot = (int)display_orientation();
        if (prefs_sync((int)i18n_selected(), pref_rot,
                       display_auto_rotate()) == 1) {
            umos_logf("prefs: saved language %d, orientation %d, auto %d\n",
                      (int)i18n_selected(), pref_rot, display_auto_rotate());
        }
        usb_gadget_poll();
        usbui_draw();
        board_harness();
        /* A transition asks for the next frame as fast as the panel can take
         * it; an idle shell sleeps instead of burning the core spinning out
         * identical frames. */
        if (!compositor_needs_frame()) sysio_sleep_ms(8);
    }

    umos_logf("boot: exit\n");
    return 0;
}

int main(void) {
    return qeaf_main();
}
