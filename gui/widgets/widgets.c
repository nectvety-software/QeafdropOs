/* SPDX-License-Identifier: MIT */
/* gui/widgets/widgets.c - Widget base implementation */

#include "widgets.h"
#include "theme.h"
#include <stddef.h>

void widget_init(struct widget *w, int16_t x, int16_t y, uint16_t w_, uint16_t h) {
    w->bounds.x = x;
    w->bounds.y = y;
    w->bounds.w = w_;
    w->bounds.h = h;
    w->visible = 1;
    w->draw = widget_draw_rect;
    w->event = NULL;
}

void widget_draw_rect(struct widget *w) {
    if (!w->visible) return;
    gfx_fill_rect(&w->bounds, g_theme.surface);
    gfx_draw_rect(&w->bounds, g_theme.border);
}
