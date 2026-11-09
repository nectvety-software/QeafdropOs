/* SPDX-License-Identifier: MIT */
/* gui/widgets/widgets.h - Widget base types */

#ifndef UMOS_WIDGETS_H
#define UMOS_WIDGETS_H

#include "gfx.h"

struct widget {
    struct umos_rect bounds;
    int visible;
    void (*draw)(struct widget *w);
    int (*event)(struct widget *w, uint32_t type, uint32_t d1, uint32_t d2);
};

void widget_init(struct widget *w, int16_t x, int16_t y, uint16_t w_, uint16_t h);
void widget_draw_rect(struct widget *w);

#endif
