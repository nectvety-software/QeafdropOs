/* SPDX-License-Identifier: MIT */
/* gui/widgets/text_input.c - Single-line text input */

#include "text_input.h"
#include "gfx.h"
#include "theme.h"
#include <string.h>

void text_input_init(struct text_input *ti, struct font *f,
                     int16_t x, int16_t y, uint16_t w, uint16_t h) {
    widget_init(&ti->base, x, y, w, h);
    ti->font = f;
    ti->text[0] = '\0';
    ti->cursor_pos = 0;
    ti->active = 0;
    ti->text_x = x + 4;
    ti->text_y = y + (h / 2) - (f->height / 2);
    ti->base.draw = (void (*)(struct widget *))text_input_draw;
    ti->base.event = (int (*)(struct widget *, uint32_t, uint32_t, uint32_t))text_input_event;
}

void text_input_draw(struct text_input *ti) {
    if (!ti->base.visible) return;
    /* Background */
    gfx_fill_rect(&ti->base.bounds, ti->active ? g_theme.primary : g_theme.surface);
    gfx_draw_rect(&ti->base.bounds, g_theme.border);
    /* Text */
    if (ti->font) {
        font_draw_string(ti->font, ti->text, ti->text_x, ti->text_y,
                         ti->active ? g_theme.on_primary : g_theme.text);
    }
}

int text_input_event(struct text_input *ti, uint32_t type, uint32_t d1, uint32_t d2) {
    (void)d2;
    if (!ti->base.visible) return 0;
    if (type == 1) { /* activate */
        ti->active = 1;
        return 1;
    }
    if (type == 2) { /* deactivate */
        ti->active = 0;
        return 1;
    }
    if (type == 3 && ti->active) { /* char input */
        char c = (char)d1;
        if (c >= 32 && c < 127) {
            int len = (int)strlen(ti->text);
            if (len < TEXT_INPUT_MAX_LEN - 1) {
                memmove(ti->text + ti->cursor_pos + 1,
                        ti->text + ti->cursor_pos,
                        len - ti->cursor_pos + 1);
                ti->text[ti->cursor_pos] = c;
                ti->cursor_pos++;
                return 1;
            }
        }
    }
    if (type == 4 && ti->active) { /* backspace */
        if (ti->cursor_pos > 0) {
            int len = (int)strlen(ti->text);
            memmove(ti->text + ti->cursor_pos - 1,
                    ti->text + ti->cursor_pos,
                    len - ti->cursor_pos + 1);
            ti->cursor_pos--;
            return 1;
        }
    }
    if (type == 5 && ti->active) { /* delete */
        int len = (int)strlen(ti->text);
        if (ti->cursor_pos < len) {
            memmove(ti->text + ti->cursor_pos,
                    ti->text + ti->cursor_pos + 1,
                    len - ti->cursor_pos);
            return 1;
        }
    }
    if (type == 6 && ti->active) { /* left */
        if (ti->cursor_pos > 0) ti->cursor_pos--;
        return 1;
    }
    if (type == 7 && ti->active) { /* right */
        int len = (int)strlen(ti->text);
        if (ti->cursor_pos < len) ti->cursor_pos++;
        return 1;
    }
    return 0;
}

void text_input_clear(struct text_input *ti) {
    ti->text[0] = '\0';
    ti->cursor_pos = 0;
}

const char *text_input_text(struct text_input *ti) {
    return ti->text;
}
