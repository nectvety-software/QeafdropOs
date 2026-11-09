/* SPDX-License-Identifier: MIT */
/* gui/gfx/theme.h - system palette, dark/light mode, Material You source. */

#ifndef UMOS_THEME_H
#define UMOS_THEME_H

#include "gfx.h"

struct umos_theme {
    struct umos_color bg;
    struct umos_color surface;
    struct umos_color primary;
    struct umos_color on_primary;
    struct umos_color secondary;
    struct umos_color on_secondary;
    struct umos_color text;
    struct umos_color border;
};

extern struct umos_theme g_theme;

/* 0 = light, 1 = dark (OLED-friendly: true-black bg, no lifted surfaces). */
enum theme_mode { THEME_LIGHT = 0, THEME_DARK = 1 };

#define THEME_ICON_PACKS 3
#define THEME_FONT_SCALE_MIN 1
#define THEME_FONT_SCALE_MAX 3

void theme_init(void);
void theme_set_mode(enum theme_mode m);
enum theme_mode theme_mode(void);

/* Derives the whole accent ramp from real wallpaper RGB565 samples. Picks the
 * most saturated sample (integer max-min), then builds tonal variants with
 * integer lerps. No floats, no colour library, no network. */
void theme_from_wallpaper(const uint16_t *samples, int n);
/* Samples gui/gfx/wallpaper.c's generated scene and applies it. Returns the
 * number of samples actually used (0 = wallpaper not ready). */
int theme_apply_wallpaper(void);

/* Accent the derivation chose, as RGB565. Stable across dark/light toggles. */
uint16_t theme_accent565(void);
struct umos_color theme_accent(void);
int theme_dynamic(void);

/* Font scale and icon pack are real, read by the app registry / launcher. */
void theme_set_font_scale(int scale);
int  theme_font_scale(void);
void theme_set_icon_pack(int pack);
int  theme_icon_pack(void);
const char *theme_icon_pack_name(int pack);

#endif
