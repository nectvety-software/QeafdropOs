/* SPDX-License-Identifier: MIT */
/* apps/gallery/gallery.c - Gallery app */

#include "gui/window_manager.h"
#include "gui/gfx/theme.h"
#include "gui/gfx/gfx.h"

static struct wm_window g_gallery_win;

static void gallery_draw(struct wm_window *w) {
    struct umos_rect full = {0, 0, (uint16_t)gfx_width(), (uint16_t)gfx_height()};
    gfx_fill_rect(&full, g_theme.bg);
    struct umos_rect card = {20, 20, (uint16_t)(gfx_width() - 40), (uint16_t)(gfx_height() - 40)};
    gfx_fill_rect(&card, g_theme.surface);
    gfx_draw_rect(&card, g_theme.border);
}

static int gallery_event(struct wm_window *w, uint32_t type, uint32_t d1, uint32_t d2) {
    (void)w; (void)type; (void)d1; (void)d2;
    return 0;
}

void gallery_init(void) {
    g_gallery_win.name = "gallery";
    g_gallery_win.bounds = (struct umos_rect){0, 0, 1080, 2340};
    g_gallery_win.visible = 0;
    g_gallery_win.focused = 0;
    g_gallery_win.draw = gallery_draw;
    g_gallery_win.event = gallery_event;
    wm_register_window(UMOS_APP_GALLERY, &g_gallery_win);
}
