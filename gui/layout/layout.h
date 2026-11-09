/* SPDX-License-Identifier: MIT */
/* gui/layout/layout.h - Simple layout helpers */

#ifndef UMOS_LAYOUT_H
#define UMOS_LAYOUT_H

#include "gfx.h"

struct layout_container {
    struct umos_rect bounds;
    int16_t padding;
    int16_t spacing;
};

void layout_init(struct layout_container *lc, int16_t x, int16_t y,
                 uint16_t w, uint16_t h, int16_t padding, int16_t spacing);
struct umos_rect layout_next_row(struct layout_container *lc, uint16_t h);
struct umos_rect layout_next_col(struct layout_container *lc, uint16_t w);
struct umos_rect layout_fill(struct layout_container *lc);
void layout_reset(struct layout_container *lc);

#endif
