/* SPDX-License-Identifier: MIT */
/* gui/widgets/statusbar.c - Status bar rendering */

#include "statusbar.h"
#include "gfx.h"
#include "theme.h"
#include <stddef.h>
#include <string.h>

static char g_time_buf[16] = "12:00";
static int g_battery = 85;
static int g_wifi_rssi = -60;
static int g_signal = 4;

void statusbar_init(struct statusbar *sb, struct font *f) {
    sb->font = f;
    sb->on_wifi_click = NULL;
    sb->on_battery_click = NULL;
    sb->on_clock_click = NULL;
    strncpy(g_time_buf, "12:00", sizeof(g_time_buf) - 1);
    g_battery = 85;
    g_wifi_rssi = -60;
    g_signal = 4;
}

void statusbar_set_time(struct statusbar *sb, const char *time) {
    (void)sb;
    if (time) strncpy(g_time_buf, time, sizeof(g_time_buf) - 1);
}

void statusbar_set_battery(struct statusbar *sb, int level) {
    (void)sb;
    if (level < 0) level = 0;
    if (level > 100) level = 100;
    g_battery = level;
}

void statusbar_set_wifi(struct statusbar *sb, int rssi) {
    (void)sb;
    g_wifi_rssi = rssi;
}

void statusbar_set_signal(struct statusbar *sb, int bars) {
    (void)sb;
    if (bars < 0) bars = 0;
    if (bars > 4) bars = 4;
    g_signal = bars;
}

void statusbar_draw(struct statusbar *sb) {
    uint32_t w = gfx_width();
    if (w == 0) return;
    struct umos_rect bar = {0, 0, (uint16_t)w, 24};
    gfx_fill_rect(&bar, g_theme.surface);
    gfx_draw_rect(&bar, g_theme.border);
    if (!sb->font) return;

    /* Left: time */
    font_draw_string(sb->font, g_time_buf, 8, 4, g_theme.text);

    /* Center: maybe title or status */
    /* Right: signal, wifi, battery - simple text */
    char buf[32];
    uint32_t x = w - 8;
    /* Battery: "85%" */
    snprintf(buf, sizeof(buf), "%d%%", g_battery);
    x -= (uint32_t)font_string_width(sb->font, buf);
    font_draw_string(sb->font, buf, (int16_t)x, 4, g_theme.text);
    x -= 16;
    /* Wifi: "Wi-Fi -60" */
    snprintf(buf, sizeof(buf), "Wi-Fi %d", g_wifi_rssi);
    x -= (uint32_t)font_string_width(sb->font, buf);
    font_draw_string(sb->font, buf, (int16_t)x, 4, g_theme.text);
    x -= 16;
    /* Signal bars: "4" or similar */
    if (g_signal > 0) {
        snprintf(buf, sizeof(buf), "%d", g_signal);
        x -= (uint32_t)font_string_width(sb->font, buf);
        font_draw_string(sb->font, buf, (int16_t)x, 4, g_theme.text);
        x -= 8;
    }
}
