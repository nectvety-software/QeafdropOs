/* SPDX-License-Identifier: MIT */
/* gui/widgets/button.h - Button widget */

#ifndef UMOS_BUTTON_H
#define UMOS_BUTTON_H

#include "widgets.h"

struct button {
    struct widget base;
    const char *label;
    void (*on_click)(struct button *b);
};

void button_init(struct button *b, int16_t x, int16_t y, uint16_t w, uint16_t h,
                 const char *label, void (*on_click)(struct button *b));
void button_draw(struct button *b);

#endif
