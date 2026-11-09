/* SPDX-License-Identifier: MIT */
/* gui/gfx/theme.c - palette, dark mode and wallpaper-driven colour.
 *
 * "Dynamic / Material You" here is honest: it is a deterministic integer
 * ramp derived from pixels that really exist in the generated wallpaper.
 * There is no colour-quantisation library and no model. Dark mode uses a
 * true-black background (OLED pixels off) rather than a grey lift.
 */

#include "theme.h"
#include "wallpaper.h"
#include <stdint.h>

struct umos_theme g_theme;

static enum theme_mode g_mode = THEME_DARK;
/* RGB565 of #1A73E8, the accent every Material screen in this shell uses. */
static uint16_t g_accent = 0x1B9D;
static int g_dynamic;                   /* 1 once derived from wallpaper */
static int g_font_scale = 1;
static int g_icon_pack = 0;

static const char *const ICON_PACKS[THEME_ICON_PACKS] = {
    "VQEAF", "Rounded", "Glyph"
};

static struct umos_color C(uint8_t r, uint8_t g, uint8_t b) {
    struct umos_color c = { r, g, b, 0xFF };
    return c;
}

static uint16_t pack565(int r, int g, int b) {
    if (r < 0) r = 0; if (r > 255) r = 255;
    if (g < 0) g = 0; if (g > 255) g = 255;
    if (b < 0) b = 0; if (b > 255) b = 255;
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

static void unpack(int v, int *r, int *g, int *b) {
    *r = ((v >> 11) & 31) * 255 / 31;
    *g = ((v >> 5) & 63) * 255 / 63;
    *b = (v & 31) * 255 / 31;
}

static struct umos_color from565(uint16_t v) {
    int r, g, b;
    unpack(v, &r, &g, &b);
    return C((uint8_t)r, (uint8_t)g, (uint8_t)b);
}

uint16_t theme_accent565(void) { return g_accent; }
struct umos_color theme_accent(void) { return from565(g_accent); }
int theme_dynamic(void) { return g_dynamic; }

/* Applies the current accent + mode to the eight system colours. */
static void apply(void) {
    int ar, ag, ab;
    uint16_t light;

    unpack(g_accent, &ar, &ag, &ab);
    light = pack565(ar + (255 - ar) / 3, ag + (255 - ag) / 3,
                    ab + (255 - ab) / 3);

    g_theme.primary = from565(g_accent);
    g_theme.secondary = from565(light);
    g_theme.on_primary = C(0xFF, 0xFF, 0xFF);
    g_theme.on_secondary = C(0x20, 0x21, 0x24);

    if (g_mode == THEME_DARK) {
        /* True-black background keeps the OLED claim; the raised surface is
         * the neutral Material grey rather than a tinted accent. */
        g_theme.bg = C(0x00, 0x00, 0x00);
        g_theme.surface = C(0x1E, 0x1E, 0x1E);
        g_theme.text = C(0xE8, 0xEA, 0xED);
        g_theme.border = C(0x3C, 0x40, 0x43);
    } else {
        g_theme.bg = C(0xFF, 0xFF, 0xFF);
        g_theme.surface = C(0xF1, 0xF3, 0xF4);
        g_theme.text = C(0x20, 0x21, 0x24);
        g_theme.border = C(0xDA, 0xDC, 0xE0);
    }
}

void theme_init(void) {
    g_mode = THEME_DARK;
    g_accent = 0x1B9D;
    g_dynamic = 0;
    g_font_scale = 1;
    g_icon_pack = 0;
    apply();
}

void theme_set_mode(enum theme_mode m) {
    if (m != THEME_LIGHT && m != THEME_DARK) return;
    g_mode = m;
    apply();
}

enum theme_mode theme_mode(void) { return g_mode; }

/* Saturation in RGB565 space is just max-min of the unpacked channels. */
static int saturation(uint16_t v) {
    int r, g, b, hi, lo;
    unpack(v, &r, &g, &b);
    hi = r > g ? r : g; if (b > hi) hi = b;
    lo = r < g ? r : g; if (b < lo) lo = b;
    return hi - lo;
}

void theme_from_wallpaper(const uint16_t *samples, int n) {
    int best = -1, best_sat = 24;   /* ignore near-greys */
    int i;

    if (!samples || n <= 0) return;
    for (i = 0; i < n; i++) {
        int s = saturation(samples[i]);
        if (s > best_sat) { best_sat = s; best = i; }
    }
    if (best < 0) return;            /* all grey: keep the default accent */
    g_accent = samples[best];
    g_dynamic = 1;
    apply();
}

int theme_apply_wallpaper(void) {
    uint16_t samples[12];
    int n = wallpaper_sample_palette(samples, 12);
    if (!n) return 0;
    theme_from_wallpaper(samples, n);
    return n;
}

void theme_set_font_scale(int scale) {
    if (scale < THEME_FONT_SCALE_MIN) scale = THEME_FONT_SCALE_MIN;
    if (scale > THEME_FONT_SCALE_MAX) scale = THEME_FONT_SCALE_MAX;
    g_font_scale = scale;
}

int theme_font_scale(void) { return g_font_scale; }

void theme_set_icon_pack(int pack) {
    if (pack < 0 || pack >= THEME_ICON_PACKS) return;
    g_icon_pack = pack;
}

int theme_icon_pack(void) { return g_icon_pack; }

const char *theme_icon_pack_name(int pack) {
    if (pack < 0 || pack >= THEME_ICON_PACKS) return "unknown";
    return ICON_PACKS[pack];
}
