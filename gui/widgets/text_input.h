/* SPDX-License-Identifier: MIT */
/* gui/widgets/text_input.h - Single-line text input */

#ifndef UMOS_TEXT_INPUT_H
#define UMOS_TEXT_INPUT_H

#include "widgets.h"
#include "font.h"

#define TEXT_INPUT_MAX_LEN 64

struct text_input {
    struct widget base;
    struct font *font;
    char text[TEXT_INPUT_MAX_LEN];
    int cursor_pos;
    int active;
    int16_t text_x;
    int16_t text_y;
};

void text_input_init(struct text_input *ti, struct font *f,
                     int16_t x, int16_t y, uint16_t w, uint16_t h);
void text_input_draw(struct text_input *ti);
int text_input_event(struct text_input *ti, uint32_t type, uint32_t d1, uint32_t d2);
void text_input_clear(struct text_input *ti);
const char *text_input_text(struct text_input *ti);

#endif
