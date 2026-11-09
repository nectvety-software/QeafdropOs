/* SPDX-License-Identifier: MIT */
/* gui/widgets/button.c - Button widget */

#include "button.h"
#include "gfx.h"
#include "theme.h"

void button_init(struct button *b, int16_t x, int16_t y, uint16_t w, uint16_t h,
                 const char *label, void (*on_click)(struct button *b)) {
    widget_init(&b->base, x, y, w, h);
    b->label = label;
    b->on_click = on_click;
    b->base.draw = (void (*)(struct widget *))button_draw;
}

void button_draw(struct button *b) {
    if (!b->base.visible) return;
    gfx_fill_rect(&b->base.bounds, g_theme.primary);
    gfx_draw_rect(&b->base.bounds, g_theme.border);
    /* Label drawing would go here (needs font) */
}
