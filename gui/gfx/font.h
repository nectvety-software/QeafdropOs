/* SPDX-License-Identifier: MIT */
/* gui/gfx/font.h - Bitmap font rendering */

#ifndef UMOS_FONT_H
#define UMOS_FONT_H

#include <stdint.h>
#include "gfx.h"

struct font_glyph {
    uint8_t width;
    uint8_t height;
    uint8_t advance;
    int8_t offset_x;
    int8_t offset_y;
    const uint8_t *bitmap; /* 1 bit per pixel, MSB first */
};

struct font {
    const struct font_glyph *glyphs; /* indexed by char code */
    uint8_t first_char;
    uint8_t last_char;
    uint8_t height;
    /* composed code points beyond the 8-bit table (Vietnamese SSIDs, names);
     * NULL when the face has none */
    const struct font_glyph *(*ext)(uint32_t cp);
};

void font_init(struct font *f, const struct font_glyph *glyphs,
               uint8_t first, uint8_t last, uint8_t height);
void font_draw_char(struct font *f, char c, int16_t x, int16_t y,
                    struct umos_color color);
void font_draw_cp(struct font *f, uint32_t cp, int16_t x, int16_t y,
                  struct umos_color color);
void font_draw_string(struct font *f, const char *s, int16_t x, int16_t y,
                      struct umos_color color);
int16_t font_string_width(struct font *f, const char *s);
int16_t font_height(struct font *f);

/* Decodes one UTF-8 code point and advances *s past it. A byte that is not a
 * valid lead/continuation yields U+FFFD and advances one byte, so a broken
 * string from the radio draws a mark instead of desyncing the cursor. */
uint32_t font_utf8_next(const char **s);
const struct font_glyph *font_glyph_cp(struct font *f, uint32_t cp);

#endif
