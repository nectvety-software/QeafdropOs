/* SPDX-License-Identifier: MIT */
/* apps/browser/browser.c - Browser app */

#include "gui/window_manager.h"
#include "gui/gfx/theme.h"
#include "gui/gfx/gfx.h"

static struct wm_window g_browser_win;

static void browser_draw(struct wm_window *w) {
    struct umos_rect full = {0, 0, (uint16_t)gfx_width(), (uint16_t)gfx_height()};
    gfx_fill_rect(&full, g_theme.bg);
    struct umos_rect card = {20, 20, (uint16_t)(gfx_width() - 40), (uint16_t)(gfx_height() - 40)};
    gfx_fill_rect(&card, g_theme.surface);
    gfx_draw_rect(&card, g_theme.border);
}

static int browser_event(struct wm_window *w, uint32_t type, uint32_t d1, uint32_t d2) {
    (void)w; (void)type; (void)d1; (void)d2;
    return 0;
}

void browser_init(void) {
    g_browser_win.name = "browser";
    g_browser_win.bounds = (struct umos_rect){0, 0, 1080, 2340};
    g_browser_win.visible = 0;
    g_browser_win.focused = 0;
    g_browser_win.draw = browser_draw;
    g_browser_win.event = browser_event;
    wm_register_window(UMOS_APP_BROWSER, &g_browser_win);
}
