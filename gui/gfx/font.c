/* SPDX-License-Identifier: MIT */
/* gui/gfx/font.c - Bitmap font rendering */

#include "font.h"
#include "gfx.h"
#include <string.h>

void font_init(struct font *f, const struct font_glyph *glyphs,
               uint8_t first, uint8_t last, uint8_t height) {
    f->glyphs = glyphs;
    f->first_char = first;
    f->last_char = last;
    f->height = height;
    f->ext = NULL;
}

uint32_t font_utf8_next(const char **s) {
    const uint8_t *p = (const uint8_t *)*s;
    uint32_t cp = p[0];
    int extra = 0;

    if (cp >= 0xF0) { extra = 3; cp &= 0x07; }
    else if (cp >= 0xE0) { extra = 2; cp &= 0x0F; }
    else if (cp >= 0xC0) { extra = 1; cp &= 0x1F; }
    else { *s = (const char *)(p + 1); return cp; }
    for (int i = 0; i < extra; i++) {
        if ((p[1 + i] & 0xC0) != 0x80) {
            *s = (const char *)(p + 1);
            return 0xFFFD;
        }
        cp = (cp << 6) | (uint32_t)(p[1 + i] & 0x3F);
    }
    *s = (const char *)(p + 1 + extra);
    return cp;
}

const struct font_glyph *font_glyph_cp(struct font *f, uint32_t cp) {
    if (cp >= f->first_char && cp <= f->last_char)
        return &f->glyphs[cp - f->first_char];
    if (f->ext)
        return f->ext(cp);
    return NULL;
}

void font_draw_cp(struct font *f, uint32_t cp, int16_t x, int16_t y,
                  struct umos_color color) {
    const struct font_glyph *g = font_glyph_cp(f, cp);
    if (!g || !g->bitmap) return;
    uint16_t pixel = ((color.r >> 3) << 11) | ((color.g >> 2) << 5) | (color.b >> 3);
    int bx = x + g->offset_x;
    int by = y + g->offset_y;
    /* font_5x7 stores the 5 pixel columns in bits 5..1 of each row byte
     * (the 5-bit pattern shifted left by one). Scanning all 8 bits used to
     * paint an 8-column glyph while advancing only 6, so every letter
     * overlapped its neighbour by two columns. */
    for (uint8_t row = 0; row < g->height; row++) {
        uint8_t bits = g->bitmap[row];
        for (uint8_t col = 0; col < g->width && col < 5; col++) {
            if (bits & (0x20 >> col)) {
                /* Route through gfx_plot565 so a rotated panel sees the same
                 * glyph. Bounds are checked against LOGICAL dimensions. */
                gfx_plot565(bx + (int)col, by + (int)row, pixel);
            }
        }
    }
}

void font_draw_char(struct font *f, char c, int16_t x, int16_t y,
                    struct umos_color color) {
    font_draw_cp(f, (uint8_t)c, x, y, color);
}

void font_draw_string(struct font *f, const char *s, int16_t x, int16_t y,
                      struct umos_color color) {
    int16_t cx = x;
    while (*s) {
        const char *at = s;
        uint32_t cp = font_utf8_next(&at);
        const struct font_glyph *g = font_glyph_cp(f, cp);
        if (g) {
            font_draw_cp(f, cp, cx, y, color);
            cx += g->advance;
        } else {
            cx += 6; /* fallback advance */
        }
        s = at;
    }
}

int16_t font_string_width(struct font *f, const char *s) {
    int16_t w = 0;
    while (*s) {
        const struct font_glyph *g = font_glyph_cp(f, font_utf8_next(&s));
        w += g ? g->advance : 6;
    }
    return w;
}

int16_t font_height(struct font *f) {
    return f->height;
}
