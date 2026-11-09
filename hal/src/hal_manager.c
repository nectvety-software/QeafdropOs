/* SPDX-License-Identifier: MIT */
/* hal/src/hal_manager.c - HAL manager: init all domains */

#include "hal.h"
#include <stddef.h>

static struct hal_display *g_display = NULL;
static struct hal_touch *g_touch = NULL;
static struct hal_audio *g_audio = NULL;
static struct hal_storage *g_storage = NULL;
static struct hal_power *g_power = NULL;
static struct hal_rtc *g_rtc = NULL;
static struct hal_radio *g_radio = NULL;

int hal_init(void) {
    int rc = UMOS_OK;
    if (g_display && g_display->init) rc = g_display->init();
    if (rc != UMOS_OK) return rc;
    if (g_touch && g_touch->init) rc = g_touch->init();
    if (rc != UMOS_OK) return rc;
    if (g_audio && g_audio->init) rc = g_audio->init();
    if (rc != UMOS_OK) return rc;
    if (g_storage && g_storage->init) rc = g_storage->init();
    if (rc != UMOS_OK) return rc;
    if (g_power && g_power->init) rc = g_power->init();
    if (rc != UMOS_OK) return rc;
    if (g_rtc && g_rtc->init) rc = g_rtc->init();
    if (rc != UMOS_OK) return rc;
    if (g_radio && g_radio->init) rc = g_radio->init();
    return rc;
}

int hal_screen_w(void) { return g_display ? 0 : 0; }
int hal_screen_h(void) { return 0; }
int hal_stride(void) { return 0; }
uint16_t *hal_fb(void) { return NULL; }

void hal_register_display(struct hal_display *d) { g_display = d; }
void hal_register_touch(struct hal_touch *t) { g_touch = t; }
void hal_register_audio(struct hal_audio *a) { g_audio = a; }
void hal_register_storage(struct hal_storage *s) { g_storage = s; }
void hal_register_power(struct hal_power *p) { g_power = p; }
void hal_register_rtc(struct hal_rtc *r) { g_rtc = r; }
void hal_register_radio(struct hal_radio *r) { g_radio = r; }

struct hal_display *hal_display(void) { return g_display; }
struct hal_touch *hal_touch(void) { return g_touch; }
struct hal_audio *hal_audio(void) { return g_audio; }
struct hal_storage *hal_storage(void) { return g_storage; }
struct hal_power *hal_power(void) { return g_power; }
struct hal_rtc *hal_rtc(void) { return g_rtc; }
struct hal_radio *hal_radio(void) { return g_radio; }
