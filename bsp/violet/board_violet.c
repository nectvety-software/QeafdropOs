/* SPDX-License-Identifier: MIT */
/* bsp/violet/board_violet.c - Violet board: memory, drivers, harness */

#include "board_violet.h"
#include "hal/include/hal.h"
#include "hal/include/hal_board.h"
#include "core/log.h"

#define VIOLET_RAM_SIZE  (4 * 1024 * 1024)  /* 4MB heap for bring-up */
static uint8_t violet_ram[VIOLET_RAM_SIZE];

/* WCN3990 wifi */
extern int wifi_init(void);
extern int wifi_on(void);
extern int wifi_off(void);

/* The DRM/KMS panel (bsp/violet/panel_drm.c). These callbacks are not optional
 * decoration: violet has no fbdev, so with NULL here init/main.c skips
 * gfx_init(), gfx_fb() stays NULL and every draw is a no-op - a phone that
 * boots with a black panel and no error anywhere except one log line. */
extern int violet_panel_init(void);
extern int violet_panel_geometry(uint32_t *w, uint32_t *h, uint32_t *stride);
extern uint16_t *violet_panel_fb(void);
extern int violet_panel_flush(int x, int y, int w, int h);

/* HAL driver instances */
static struct hal_display g_display = {
    .init = violet_panel_init,
    .get_geometry = violet_panel_geometry,
    .fb = violet_panel_fb,
    .flush = violet_panel_flush,
};
static struct hal_touch g_touch = {
    .init = NULL,
    .poll = NULL,
};
static struct hal_audio g_audio = {
    .init = NULL,
    .beep = NULL,
};
static struct hal_storage g_storage = {
    .init = NULL,
    .read_sector = NULL,
    .write_sector = NULL,
    .total_sectors = NULL,
};
static struct hal_power g_power = {
    .init = NULL,
    .reboot = NULL,
    .poweroff = NULL,
};
static struct hal_rtc g_rtc = {
    .init = NULL,
    .now_ms = NULL,
};
static struct hal_radio g_radio = {
    .init = NULL,
    .wifi_on = NULL,
    .wifi_off = NULL,
};

/* WCN3990 wifi instance */
extern int wifi_init(void);
extern int wifi_on(void);
extern int wifi_off(void);

int board_memory(void **arena, size_t *arena_size) {
    *arena = violet_ram;
    *arena_size = VIOLET_RAM_SIZE;
    return UMOS_OK;
}

int board_should_exit(void) {
    return 0;
}

void board_poll(void) {
    /* Poll touch, buttons, USB, etc. */
}

void board_harness(void) {
    /* Yield to interrupt/wait for next event */
}

int board_boot_recovery(void) {
    /* TODO(violet): read the misc boot command (boot-recovery) once the real
     * rescue init is wired back in. Until then a normal boot opens the lock
     * screen and the recovery GUI stays reachable by name / recovery_open(). */
    return 0;
}

int board_boot_fastboot(void) {
    /* TODO(violet): read the boot command once the real rescue init is wired
     * back in. Until then a normal boot opens the lock screen and the fastboot
     * GUI stays reachable by name / fastboot_open(). */
    return 0;
}

int violet_board_init(void) {
    umos_log_init();
    /* Register HAL drivers with the manager */
    hal_register_display(&g_display);
    hal_register_touch(&g_touch);
    hal_register_audio(&g_audio);
    hal_register_storage(&g_storage);
    hal_register_power(&g_power);
    hal_register_rtc(&g_rtc);

    /* Register WCN3990 radio */
    g_radio.init = (int (*)(void))wifi_init;
    g_radio.wifi_on = (int (*)(void))wifi_on;
    g_radio.wifi_off = (int (*)(void))wifi_off;
    hal_register_radio(&g_radio);

    return UMOS_OK;
}

void violet_display_driver(void) { /* stub: MIPI DSI panel */ }
void violet_touch_driver(void) { /* stub: Goodix touch controller */ }
void violet_audio_driver(void) { /* stub: WCD934x codec */ }
void violet_storage_driver(void) { /* stub: UFS/eMMC via sdhc */ }
void violet_power_driver(void) { /* stub: PMIC */ }
void violet_rtc_driver(void) { /* stub: RTC */ }
void violet_radio_driver(void) { /* stub: WCN3990 WiFi/BT */ }
