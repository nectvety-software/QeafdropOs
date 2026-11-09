/* SPDX-License-Identifier: MIT */
/* apps/lock/lock_screen.c - Lock screen, plus the charge line the battery
 * really reports.
 *
 * The line is drawn from battery_read() only: no fixture answer means no line,
 * not a "100%" from the source. Reads are throttled (first paint, then every
 * 16th frame) so the main loop does not walk sysfs 60 times a second; a
 * charger unplugged mid-glance shows up on the next sampled frame.
 *
 * The dial is the system clock (core/clock.c): RTC wall time when the board
 * answers, uptime otherwise. The label under it says which, so neither is
 * passed off as the other. Portrait keeps the tall centred dial; landscape
 * moves the clock left and the notifications right.
 */

#include "apps/lock/lock_screen.h"
#include "hal/include/battery.h"
#include "apps/bootui/bootui.h"
#include "gui/window_manager.h"
#include "gui/gfx/theme.h"
#include "gui/gfx/gfx.h"
#include "core/clock.h"
#include "core/i18n.h"

static struct wm_window g_lock_win;

static struct battery_state g_batt;
static int g_batt_have;
static int g_batt_tick = 16; /* first draw always samples */

static void put_u(char *out, int cap, unsigned v) {
    char tmp[8];
    int t = 0;
    int n = 0;
    out[0] = '\0';
    if (cap < 2) return;
    do { tmp[t++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (t > 0 && n + 1 < cap) out[n++] = tmp[--t];
    out[n] = '\0';
}

/* "<Status> <pct>%", e.g. "Charging 84%" - the battery's own words. */
static void charge_line(char *line, int cap) {
    int n = 0;
    const char *s;

    line[0] = '\0';
    for (s = g_batt.status; *s && n + 1 < cap; s++) line[n++] = *s;
    if (n + 1 < cap) line[n++] = ' ';
    {
        char num[8];
        put_u(num, (int)sizeof(num), (unsigned)g_batt.capacity);
        for (s = num; *s && n + 1 < cap; s++) line[n++] = *s;
    }
    if (n + 1 < cap) line[n++] = '%';
    line[n] = '\0';
}

/* 26x13 outline with a nub, interior filled to capacity. */
static void batt_glyph(int x, int y, struct umos_color c, int pct) {
    bui_hline(x, y, 26, c);
    bui_hline(x, y + 12, 26, c);
    bui_fill(x, y, 1, 13, c);
    bui_fill(x + 25, y, 1, 13, c);
    bui_fill(x + 26, y + 4, 2, 5, c);
    if (pct > 0) {
        int fw = (24 * pct) / 100;
        if (fw > 24) fw = 24;
        bui_fill(x + 1, y + 1, fw, 11, c);
    }
}

static void draw_charge_pill(int y0) {
    char line[40];
    int tw, total, x0;

    charge_line(line, (int)sizeof(line));
    tw = bui_text_w(line, 2);
    total = 34 + tw;
    x0 = bui_vwidth() / 2 - total / 2;
    if (x0 < 26) x0 = 26;
    bui_round(x0 - 10, y0 - 7, total + 20, 28, 14,
              BUI_SURFACE, BUI_BORDER);
    batt_glyph(x0, y0, g_theme.primary, g_batt.capacity);
    bui_text(line, x0 + 34, y0 + 1, 2, g_theme.text);
}

static void lock_draw(struct wm_window *w) {
    struct umos_rect full = {0, 0, (uint16_t)gfx_width(), (uint16_t)gfx_height()};
    char now[8];
    int wall;
    (void)w;

    /* The lock screen is a sheet over the zoomed, blurred, dimmed backdrop -
     * the plate softens the wallpaper without hiding it, which is the whole
     * point of the transition into this screen. */
    gfx_blend_rect(&full, g_theme.bg, BUI_SHEET_ALPHA_Q);
    bui_view_responsive();

    if (g_batt_tick >= 16) {
        g_batt_tick = 0;
        g_batt_have = battery_read(&g_batt) == 0;
    } else {
        g_batt_tick++;
    }

    bui_statusbar(NULL, g_batt_have ? (int)g_batt.capacity : -1);

    clock_hhmm(now, (int)sizeof(now));
    wall = clock_is_wall();

    if (bui_landscape()) {
        bui_text(now, 28, 44, 10, BUI_TEXT);
        bui_text(wall ? i18n_t(I18N_TIME) : i18n_t(I18N_UP_SINCE_BOOT),
                 30, 124, 1, BUI_DIM);
        bui_text(i18n_t(I18N_LOCKED), 30, 146, 2, BUI_DIM);
        bui_text("VQEAF OS 0.1.0", 30, 172, 1, BUI_DIM);

        bui_icon(BUI_ICON_LOCK, 716, 44, BUI_TEXT);
        bui_text_center(i18n_t(I18N_NO_NOTIF), 540, 118, 2, BUI_DIM);

        bui_round(28, 292, 44, 44, 22, BUI_SURFACE, BUI_BORDER);
        bui_icon(BUI_ICON_FLASH, 42, 306, BUI_TEXT);
        bui_round(708, 292, 44, 44, 22, BUI_SURFACE, BUI_BORDER);
        bui_icon(BUI_ICON_CAMERA, 722, 306, BUI_TEXT);

        if (!g_batt_have || !g_batt.plugged) return;
        draw_charge_pill(316);
        return;
    }

    /* Portrait: the oversized dial, labelled with the clock it really is. */
    bui_text(now, 36, 88, 10, BUI_TEXT);
    bui_icon(BUI_ICON_LOCK, 173, 40, BUI_TEXT);
    bui_text(wall ? i18n_t(I18N_TIME) : i18n_t(I18N_UP_SINCE_BOOT),
             40, 164, 1, BUI_DIM);
    bui_text(i18n_t(I18N_LOCKED), 40, 188, 2, BUI_DIM);
    bui_text("VQEAF OS 0.1.0", 40, 212, 1, BUI_DIM);
    bui_text_center(i18n_t(I18N_NO_NOTIF), 180, 300, 2, BUI_DIM);

    /* Bottom shortcuts, the two round buttons a lock screen carries. */
    bui_round(24, 674, 48, 48, 24, BUI_SURFACE, BUI_BORDER);
    bui_icon(BUI_ICON_FLASH, 40, 690, BUI_TEXT);
    bui_round(288, 674, 48, 48, 24, BUI_SURFACE, BUI_BORDER);
    bui_icon(BUI_ICON_CAMERA, 304, 690, BUI_TEXT);

    if (!g_batt_have || !g_batt.plugged) return;

    /* Centered charge pill, between the two shortcuts. */
    draw_charge_pill(706);
}

static int lock_event(struct wm_window *w, uint32_t type, uint32_t d1, uint32_t d2) {
    (void)w; (void)type; (void)d1; (void)d2;
    return 0;
}

/* State the host gate measures without poking at internals. */
int lock_charge_visible(void) {
    return g_batt_have && g_batt.plugged;
}

void lock_init(void) {
    g_lock_win.name = "lock";
    g_lock_win.bounds = (struct umos_rect){0, 0, 1080, 2340};
    g_lock_win.visible = 0;
    g_lock_win.focused = 0;
    g_lock_win.draw = lock_draw;
    g_lock_win.event = lock_event;
    g_batt_have = 0;
    g_batt_tick = 16;
    wm_register_window(UMOS_APP_LOCK, &g_lock_win);
}
