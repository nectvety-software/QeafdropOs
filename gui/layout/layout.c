/* SPDX-License-Identifier: MIT */
/* gui/layout/layout.c - Layout helpers */

#include "layout.h"

void layout_init(struct layout_container *lc, int16_t x, int16_t y,
                 uint16_t w, uint16_t h, int16_t padding, int16_t spacing) {
    lc->bounds.x = x;
    lc->bounds.y = y;
    lc->bounds.w = w;
    lc->bounds.h = h;
    lc->padding = padding;
    lc->spacing = spacing;
}

struct umos_rect layout_next_row(struct layout_container *lc, uint16_t h) {
    struct umos_rect r = {lc->bounds.x + lc->padding, lc->bounds.y,
                          lc->bounds.w - lc->padding * 2, h};
    lc->bounds.y += (int16_t)h + lc->spacing;
    lc->bounds.h -= (int16_t)h + lc->spacing;
    return r;
}

struct umos_rect layout_next_col(struct layout_container *lc, uint16_t w) {
    struct umos_rect r = {lc->bounds.x, lc->bounds.y + lc->padding,
                          w, lc->bounds.h - lc->padding * 2};
    lc->bounds.x += (int16_t)w + lc->spacing;
    lc->bounds.w -= (int16_t)w + lc->spacing;
    return r;
}

struct umos_rect layout_fill(struct layout_container *lc) {
    return lc->bounds;
}

void layout_reset(struct layout_container *lc) {
    lc->bounds.y = 0;
    lc->bounds.h = 0; /* Caller should reinitialize */
}
