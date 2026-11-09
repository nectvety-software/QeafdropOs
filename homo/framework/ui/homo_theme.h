#ifndef HACHIMI_HOMO_THEME_H
#define HACHIMI_HOMO_THEME_H

#include "lvgl.h"

/* Ported from VQEAF_OS/src/core/Theme.h (Arduino, RGB565 constants) to LVGL.
 * Colors are taken from the hex values in the original header comments, so
 * the palettes match pixel-for-pixel. No heap, no bitmap. */

typedef struct {
    lv_color_t bg;
    lv_color_t panel;
    lv_color_t selected;
    lv_color_t text;
    lv_color_t dim;
    lv_color_t chrome;
    lv_color_t chrome_text;
    lv_color_t accent;
    lv_color_t danger;
    lv_color_t popup;
    lv_color_t popup_text;
    lv_color_t popup_selected;
    lv_color_t border;
} homo_theme_t;

typedef enum {
    HOMO_THEME_NIGHT = 0,
    HOMO_THEME_VQEAF20 = 9,
    HOMO_THEME_MODERN_DARK = 5,
    HOMO_THEME_CLAUDE_DAY = 7,
    HOMO_THEME_CLAUDE_NIGHT = 8,
} homo_theme_id_t;

const char *homo_theme_name(homo_theme_id_t id);
void homo_theme_get(homo_theme_id_t id, homo_theme_t *out);

#endif
