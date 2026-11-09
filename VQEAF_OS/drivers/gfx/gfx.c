/* SPDX-License-Identifier: MIT */
/* gui/gfx/gfx.c - Basic graphics primitives */

#include "gfx.h"
#include <stddef.h>

static uint16_t *g_fb = NULL;
static uint32_t g_w = 0;
static uint32_t g_h = 0;
static uint32_t g_stride = 0;
static enum gfx_rotation g_rot = GFX_ROT_0;
static int32_t g_clip_x, g_clip_y, g_clip_w, g_clip_h;
static int g_clip_on;

void gfx_set_clip(int32_t x, int32_t y, int32_t w, int32_t h) {
    g_clip_x = x; g_clip_y = y; g_clip_w = w; g_clip_h = h;
    g_clip_on = (w > 0 && h > 0);
}

void gfx_clip_reset(void) {
    g_clip_on = 0;
    g_clip_x = g_clip_y = g_clip_w = g_clip_h = 0;
}

void gfx_get_clip(int32_t *x, int32_t *y, int32_t *w, int32_t *h) {
    if (x) *x = g_clip_x;
    if (y) *y = g_clip_y;
    if (w) *w = g_clip_on ? g_clip_w : (int32_t)gfx_logical_width();
    if (h) *h = g_clip_on ? g_clip_h : (int32_t)gfx_logical_height();
}

static int in_clip(int32_t lx, int32_t ly) {
    if (!g_clip_on) return 1;
    return lx >= g_clip_x && ly >= g_clip_y &&
           lx < g_clip_x + g_clip_w && ly < g_clip_y + g_clip_h;
}

static uint16_t color565(struct umos_color c) {
    return ((c.r >> 3) << 11) | ((c.g >> 2) << 5) | (c.b >> 3);
}

void gfx_set_rotation(enum gfx_rotation r) {
    if (r < 0 || r >= GFX_ROT_COUNT) return;
    g_rot = r;
}

enum gfx_rotation gfx_rotation(void) { return g_rot; }

const char *gfx_rotation_name(enum gfx_rotation r) {
    switch (r) {
    case GFX_ROT_0:   return "0";
    case GFX_ROT_90:  return "90";
    case GFX_ROT_180: return "180";
    case GFX_ROT_270: return "270";
    default:          return "unknown";
    }
}

uint32_t gfx_logical_width(void) {
    return (g_rot == GFX_ROT_90 || g_rot == GFX_ROT_270) ? g_h : g_w;
}

uint32_t gfx_logical_height(void) {
    return (g_rot == GFX_ROT_90 || g_rot == GFX_ROT_270) ? g_w : g_h;
}

/* Maps a logical pixel to the one physical pixel that represents it. */
static void map_px(int32_t lx, int32_t ly, int32_t *px, int32_t *py) {
    int32_t w = (int32_t)g_w, h = (int32_t)g_h;
    switch (g_rot) {
    case GFX_ROT_90:  *px = ly;         *py = h - 1 - lx; break;
    case GFX_ROT_180: *px = w - 1 - lx; *py = h - 1 - ly; break;
    case GFX_ROT_270: *px = w - 1 - ly; *py = lx;         break;
    default:          *px = lx;         *py = ly;         break;
    }
}

void gfx_plot565(int32_t lx, int32_t ly, uint16_t col) {
    int32_t px, py;
    if (!g_fb) return;
    if (lx < 0 || ly < 0 ||
        lx >= (int32_t)gfx_logical_width() ||
        ly >= (int32_t)gfx_logical_height()) return;
    if (!in_clip(lx, ly)) return;
    map_px(lx, ly, &px, &py);
    if (px < 0 || py < 0 || px >= (int32_t)g_w || py >= (int32_t)g_h) return;
    g_fb[py * (g_stride / 2) + px] = col;
}

void gfx_map_touch(int32_t px, int32_t py, int32_t *lx, int32_t *ly) {
    int32_t w = (int32_t)g_w, h = (int32_t)g_h;
    if (!lx || !ly) return;
    switch (g_rot) {
    case GFX_ROT_90:  *lx = h - 1 - py; *ly = px;         break;
    case GFX_ROT_180: *lx = w - 1 - px; *ly = h - 1 - py; break;
    case GFX_ROT_270: *lx = py;         *ly = w - 1 - px; break;
    default:          *lx = px;         *ly = py;         break;
    }
}

void gfx_init(uint16_t *fb, uint32_t w, uint32_t h, uint32_t stride) {
    g_fb = fb;
    g_w = w;
    g_h = h;
    g_stride = stride;
}

void gfx_pixel(int16_t x, int16_t y, struct umos_color c) {
    gfx_plot565(x, y, color565(c));
}

void gfx_fill_rect(const struct umos_rect *r, struct umos_color c) {
    if (!g_fb || !r) return;
    uint16_t col = color565(c);
    if (g_rot == GFX_ROT_0) {
        /* Fast path: the panel is portrait, a row is contiguous memory. */
        int32_t rx0 = r->x, ry0 = r->y;
        int32_t rx1 = (int32_t)r->x + (int32_t)r->w;
        int32_t ry1 = (int32_t)r->y + (int32_t)r->h;
        if (g_clip_on) {
            if (rx0 < g_clip_x) rx0 = g_clip_x;
            if (ry0 < g_clip_y) ry0 = g_clip_y;
            if (rx1 > g_clip_x + g_clip_w) rx1 = g_clip_x + g_clip_w;
            if (ry1 > g_clip_y + g_clip_h) ry1 = g_clip_y + g_clip_h;
        }
        if (rx0 < 0) rx0 = 0;
        if (ry0 < 0) ry0 = 0;
        if (rx1 > (int32_t)g_w) rx1 = (int32_t)g_w;
        if (ry1 > (int32_t)g_h) ry1 = (int32_t)g_h;
        for (int32_t yy = ry0; yy < ry1; yy++) {
            uint16_t *row = g_fb + (uint32_t)yy * (g_stride / 2);
            for (int32_t xx = rx0; xx < rx1; xx++) row[xx] = col;
        }
        return;
    }
    for (int32_t yy = r->y; yy < r->y + (int16_t)r->h; yy++)
        for (int32_t xx = r->x; xx < r->x + (int16_t)r->w; xx++)
            gfx_plot565(xx, yy, col);
}

/* Lerp in 565 space: five and six bits are enough precision for a sheet the
 * user reads text through, and it keeps the blend free of 8-bit round trips. */
static uint16_t blend565(uint16_t dst, uint16_t src, int a) {
    int dr = (dst >> 11) & 31, dg = (dst >> 5) & 63, db = dst & 31;
    int sr = (src >> 11) & 31, sg = (src >> 5) & 63, sb = src & 31;
    int r = dr + (sr - dr) * a / 256;
    int g = dg + (sg - dg) * a / 256;
    int b = db + (sb - db) * a / 256;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

void gfx_blend_rect(const struct umos_rect *r, struct umos_color c, int alpha_q) {
    if (!g_fb || !r) return;
    if (alpha_q <= 0) return;
    if (alpha_q >= 256) { gfx_fill_rect(r, c); return; }
    uint16_t col = color565(c);
    int32_t y0 = r->y, x0 = r->x;
    int32_t y1 = (int32_t)r->y + (int32_t)r->h;
    int32_t x1 = (int32_t)r->x + (int32_t)r->w;
    if (g_clip_on) {
        if (x0 < g_clip_x) x0 = g_clip_x;
        if (y0 < g_clip_y) y0 = g_clip_y;
        if (x1 > g_clip_x + g_clip_w) x1 = g_clip_x + g_clip_w;
        if (y1 > g_clip_y + g_clip_h) y1 = g_clip_y + g_clip_h;
    }
    if (y0 < 0) y0 = 0;
    if (x0 < 0) x0 = 0;
    if (y1 > (int32_t)gfx_logical_height()) y1 = (int32_t)gfx_logical_height();
    if (x1 > (int32_t)gfx_logical_width()) x1 = (int32_t)gfx_logical_width();
    for (int32_t yy = y0; yy < y1; yy++) {
        for (int32_t xx = x0; xx < x1; xx++) {
            int32_t px, py;
            map_px(xx, yy, &px, &py);
            if (px < 0 || py < 0 || px >= (int32_t)g_w || py >= (int32_t)g_h)
                continue;
            uint16_t *p = g_fb + (uint32_t)py * (g_stride / 2) + px;
            *p = blend565(*p, col, alpha_q);
        }
    }
}

void gfx_draw_rect(const struct umos_rect *r, struct umos_color c) {
    struct umos_rect top = *r;
    top.h = 1;
    gfx_fill_rect(&top, c);
    struct umos_rect bottom = *r;
    bottom.y = r->y + (int16_t)r->h - 1;
    bottom.h = 1;
    gfx_fill_rect(&bottom, c);
    struct umos_rect left = *r;
    left.w = 1;
    gfx_fill_rect(&left, c);
    struct umos_rect right = *r;
    right.x = r->x + (int16_t)r->w - 1;
    right.w = 1;
    gfx_fill_rect(&right, c);
}

void gfx_flush(int x, int y, int w, int h) {
    /* No-op for now; HAL will handle actual panel flush */
    (void)x; (void)y; (void)w; (void)h;
}

uint32_t gfx_width(void) { return g_w; }
uint32_t gfx_height(void) { return g_h; }
uint32_t gfx_stride(void) { return g_stride; }
uint16_t *gfx_fb(void) { return g_fb; }
