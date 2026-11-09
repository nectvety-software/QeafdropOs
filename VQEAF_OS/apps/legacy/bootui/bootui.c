/* SPDX-License-Identifier: MIT */
/* apps/bootui/bootui.c - Procedural art shared by the fastboot / recovery UIs.
 *
 * See bootui.h. All drawing goes straight to the RGB565 framebuffer: the gfx
 * primitives take virtual rectangles and would round a 1px hairline up to K
 * device pixels, which is why blit() works in real pixels.
 */

#include "apps/bootui/bootui.h"

#include "gui/gfx/gfx.h"
#include "gui/gfx/font.h"
#include "gui/gfx/font_5x7.h"

#include <stddef.h>

static const int8_t bui_sin64[64] = {
       0,  12,  25,  37,  49,  60,  71,  81,
      90,  98, 106, 112, 117, 122, 125, 126,
     127, 126, 125, 122, 117, 112, 106,  98,
      90,  81,  71,  60,  49,  37,  25,  12,
       0, -12, -25, -37, -49, -60, -71, -81,
     -90, -98,-106,-112,-117,-122,-125,-126,
    -127,-126,-125,-122,-117,-112,-106, -98,
     -90, -81, -71, -60, -49, -37, -25, -12,
};

/* The canvas a screen draws in. Portrait is 360x780; a rotated panel is
 * 780x360 (2340x1080 at K=3), so the same primitives serve both orientations
 * and only the layout differs. g_view_ox/oy centre the legacy fit screens. */
static int g_view_w = BUI_V_W;
static int g_view_h = BUI_V_H;
static int g_view_k = 3;
static int g_view_ox;
static int g_view_oy;

static int fit_scale(void) {
    uint32_t lw = gfx_logical_width(), lh = gfx_logical_height();
    int kw = (int)(lw / BUI_V_W);
    int kh = (int)(lh / BUI_V_H);
    int k = kw < kh ? kw : kh;
    return k < 1 ? 1 : k;
}

static void set_view(int w, int h, int k, int ox, int oy) {
    g_view_w = w;
    g_view_h = h;
    g_view_k = k < 1 ? 1 : k;
    g_view_ox = ox;
    g_view_oy = oy;
}

void bui_view_responsive(void) {
    uint32_t lw = gfx_logical_width(), lh = gfx_logical_height();
    if (lh > lw) {
        set_view(BUI_V_W, BUI_V_H, fit_scale(), 0, 0);
    } else {
        int kw = (int)(lw / 780u);
        int kh = (int)(lh / 360u);
        int k = kw < kh ? kw : kh;
        if (k < 1) k = 1;
        set_view(780, 360, k, 0, 0);
    }
}

void bui_view_fit(void) {
    uint32_t lw = gfx_logical_width(), lh = gfx_logical_height();
    int k = fit_scale();
    int w = (int)(lw / (uint32_t)k);
    int h = (int)(lh / (uint32_t)k);
    set_view(BUI_V_W, BUI_V_H, k, (w - BUI_V_W) / 2, (h - BUI_V_H) / 2);
}

void bui_view_legacy(void) {
    set_view(BUI_V_W, BUI_V_H, fit_scale(), 0, 0);
}

int bui_scale(void) { return g_view_k; }
int bui_vwidth(void) { return g_view_w; }
int bui_vheight(void) { return g_view_h; }
int bui_landscape(void) { return g_view_w > g_view_h; }

void bui_map_touch(int px, int py, int *vx, int *vy) {
    int32_t lx = 0, ly = 0;
    gfx_map_touch(px, py, &lx, &ly);
    if (vx) *vx = (int)((lx - g_view_ox) / g_view_k);
    if (vy) *vy = (int)((ly - g_view_oy) / g_view_k);
}

static int PX(int v) { return v * g_view_k; }

static uint16_t c565(struct umos_color c) {
    return (uint16_t)(((c.r >> 3) << 11) | ((c.g >> 2) << 5) | (c.b >> 3));
}

static struct umos_color mix(struct umos_color a, struct umos_color b, int t) {
    if (t < 0) t = 0;
    if (t > 256) t = 256;
    return BUI_COL(a.r + (((int)b.r - (int)a.r) * t >> 8),
                   a.g + (((int)b.g - (int)a.g) * t >> 8),
                   a.b + (((int)b.b - (int)a.b) * t >> 8));
}

/* x/y/w/h are LOGICAL device pixels here; gfx_plot565 rotates each one. */
static void blit(int x, int y, int w, int h, uint16_t col) {
    int x0 = x + g_view_ox, y0 = y + g_view_oy;
    int x1 = x0 + w, y1 = y0 + h;
    if (!gfx_fb()) return;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > (int)gfx_logical_width()) x1 = (int)gfx_logical_width();
    if (y1 > (int)gfx_logical_height()) y1 = (int)gfx_logical_height();
    for (int yy = y0; yy < y1; yy++)
        for (int xx = x0; xx < x1; xx++)
            gfx_plot565(xx, yy, col);
}

/* Integer square root by binary search: avoids libm in the freestanding ELF. */
static int isqrt_i(int v) {
    int lo = 0, hi = 1024;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (mid * mid <= v) lo = mid; else hi = mid - 1;
    }
    return lo;
}

static int g_font_ready = 0;

static void ensure_font(void) {
    if (!g_font_ready) { font_5x7_init(); g_font_ready = 1; }
}

void bui_begin(void) {
    ensure_font();
    bui_view_responsive();
    bui_fill(0, 0, g_view_w, g_view_h, BUI_BG);
}

void bui_begin_fit(void) {
    struct umos_rect full = {0, 0, (uint16_t)gfx_logical_width(),
                             (uint16_t)gfx_logical_height()};
    ensure_font();
    bui_view_fit();
    gfx_fill_rect(&full, BUI_BG);
}

void bui_begin_overlay(void) {
    struct umos_rect full = {0, 0, (uint16_t)gfx_logical_width(),
                             (uint16_t)gfx_logical_height()};
    ensure_font();
    bui_view_responsive();
    gfx_blend_rect(&full, BUI_BG, BUI_SHEET_ALPHA_Q);
}

void bui_end(void) {
    gfx_flush(0, 0, (int)gfx_logical_width(), (int)gfx_logical_height());
}

void bui_fill(int x, int y, int w, int h, struct umos_color c) {
    blit(PX(x), PX(y), PX(w), PX(h), c565(c));
}

void bui_hline(int x, int y, int w, struct umos_color c) {
    blit(PX(x), PX(y), PX(w), 1, c565(c));
}

/* Chamfered corners: cheap, deterministic, reads as a soft corner at K=3. */
static void fill_round(int X, int Y, int W, int H, int R, uint16_t col) {
    if (R > H / 2) R = H / 2;
    if (R > W / 2) R = W / 2;
    if (R < 1) { blit(X, Y, W, H, col); return; }
    for (int yy = 0; yy < H; yy++) {
        int inset = 0;
        if (yy < R) inset = R - yy - 1;
        else if (yy >= H - R) inset = yy - (H - R);
        if (inset < 0) inset = 0;
        blit(X + inset, Y + yy, W - 2 * inset, 1, col);
    }
}

void bui_round(int x, int y, int w, int h, int r,
               struct umos_color fill, struct umos_color border) {
    int k = g_view_k;
    fill_round(PX(x), PX(y), PX(w), PX(h), PX(r), c565(border));
    fill_round(PX(x) + k, PX(y) + k, PX(w) - 2 * k, PX(h) - 2 * k,
               PX(r) - k > 0 ? PX(r) - k : 1, c565(fill));
}

void bui_gradient(int x, int y, int w, int h,
                  struct umos_color a, struct umos_color b) {
    int X = PX(x), Y = PX(y), W = PX(w), H = PX(h);
    for (int yy = 0; yy < H; yy++) {
        int t = H > 1 ? (yy * 256) / (H - 1) : 0;
        blit(X, Y + yy, W, 1, c565(mix(a, b, t)));
    }
}

void bui_dashed(int x, int y, int w, int h, struct umos_color c) {
    for (int xx = x; xx < x + w; xx += 8) {
        int n = (x + w - xx) < 5 ? (x + w - xx) : 5;
        bui_hline(xx, y, n, c);
        bui_hline(xx, y + h - 1, n, c);
    }
    for (int yy = y; yy < y + h; yy += 8) {
        int n = (y + h - yy) < 5 ? (y + h - yy) : 5;
        blit(PX(x), PX(yy), 1, PX(n), c565(c));
        blit(PX(x + w - 1), PX(yy), 1, PX(n), c565(c));
    }
}

int bui_text_w(const char *s, int scale) {
    ensure_font();
    struct font *f = &font_5x7;
    int units = 0;
    while (*s) {
        const struct font_glyph *g = font_glyph_cp(f, font_utf8_next(&s));
        units += g ? g->advance : 6;
    }
    return units * scale;
}

void bui_text(const char *s, int x, int y, int scale, struct umos_color c) {
    ensure_font();
    struct font *f = &font_5x7;
    int s_px = scale * g_view_k;
    int cx = PX(x), cy = PX(y);
    uint16_t col = c565(c);
    while (*s) {
        uint32_t cp = font_utf8_next(&s);
        const struct font_glyph *g = font_glyph_cp(f, cp);
        int adv = g ? g->advance : 6;
        if (g && g->bitmap) {
            /* font_5x7 keeps 5 columns in bits 5..1 (pattern << 1). */
            for (int row = 0; row < g->height; row++) {
                uint8_t bits = g->bitmap[row];
                for (int column = 0; column < g->width && column < 5; column++) {
                    if (bits & (0x20 >> column))
                        blit(cx + column * s_px, cy + row * s_px,
                             s_px, s_px, col);
                }
            }
        }
        cx += adv * s_px;
    }
}

void bui_text_center(const char *s, int cx, int y, int scale,
                     struct umos_color c) {
    bui_text(s, cx - bui_text_w(s, scale) / 2, y, scale, c);
}

/* --------------------------------------------------------------- brand mark */

/* The VQEAF mark: a chevron whose top edge has a V-shaped notch, a bright left
 * wing, a darker folded-over right wing and a specular leading edge. */
void bui_vlogo(int x, int y, int w, int h) {
    int X = PX(x), Y = PX(y), W = PX(w), H = PX(h);
    if (W < 8 || H < 8) return;
    int notch_h = H * 30 / 100;
    struct umos_color fold = mix(BUI_TEAL_DIM, BUI_BG_DEEP, 120);
    for (int row = 0; row < H; row++) {
        int den = H > 1 ? H - 1 : 1;
        int t = (row * 256) / den;
        int left = (W / 2) * row / den;
        int right = W - left;
        int nh = row < notch_h ? (W * 17 / 100) * (notch_h - row) / notch_h : 0;

        int a0 = X + left, a1 = X + W / 2 - nh;
        int b0 = X + W / 2 + nh, b1 = X + right;
        if (a1 > a0)
            blit(a0, Y + row, a1 - a0, 1, c565(mix(BUI_CYAN, BUI_TEAL, t)));
        if (b1 > b0)
            blit(b0, Y + row, b1 - b0, 1,
                 c565(mix(BUI_TEAL, BUI_TEAL_DIM, t)));

        /* Dark fold sweeping from the top-right down to the point. */
        int fc = X + right - (W * 12 / 100) * (H - row) / H - W * 6 / 100;
        int fw = W * 9 / 100;
        int f0 = fc - fw / 2, f1 = fc + fw / 2;
        if (f0 < b0) f0 = b0;
        if (f1 > b1) f1 = b1;
        if (f1 > f0) blit(f0, Y + row, f1 - f0, 1, c565(fold));

        if (a1 > a0) blit(a0, Y + row, 1, 1, c565(BUI_TEXT));
    }
}

void bui_rings(int cx, int cy, int r) {
    int CX = PX(cx), CY = PX(cy), R = PX(r), k = g_view_k;
    int th = k < 2 ? 2 : k;             /* hairlines vanish when downscaled */
    int radii[3];
    struct umos_color cols[3] = { BUI_TEAL, BUI_TEAL_DIM, BUI_CYAN };
    radii[0] = R;
    radii[1] = R - 6 * k;
    radii[2] = R - 13 * k;
    for (int ring = 0; ring < 3; ring++) {
        int rr = radii[ring];
        if (rr <= 0) continue;
        uint16_t col = c565(cols[ring]);
        for (int dy = -rr; dy <= rr; dy++) {
            int half = isqrt_i(rr * rr - dy * dy);
            blit(CX - half, CY + dy, th, 1, col);
            blit(CX + half - th + 1, CY + dy, th, 1, col);
        }
        /* A short continuous arc highlight, so the rings are not flat. */
        if (ring == 0) {
            for (int dx = -rr / 2; dx < 0; dx++) {
                int dy = -isqrt_i(rr * rr - dx * dx);
                blit(CX + dx, CY + dy, th, 1, c565(BUI_TEXT));
            }
        }
    }
}

void bui_wordmark(int cx, int y, int scale) {
    const char *a = "VQEAF";
    const char *b = "OS";
    int wa = bui_text_w(a, scale);
    int gap = 2 * scale;                /* a normal space is 6 units: too wide */
    int total = wa + gap + bui_text_w(b, scale);
    int x0 = cx - total / 2;
    bui_text(a, x0, y, scale, BUI_TEXT);
    bui_text(b, x0 + wa + gap, y, scale, BUI_CYAN);
}

void bui_waves(void) {
    /* Start below the footer hint / Tips box, otherwise the ribbons sit on top
     * of the copy. Only the thin crests are bright. */
    static const int base[3]   = { 730, 750, 768 };
    static const int amp[3]    = { 10, 8, 6 };
    static const int thick[3]  = { 22, 20, 16 };
    static const int phase[3]  = { 0, 90, 180 };
    struct umos_color cols[3]  = { BUI_WAVE_1, BUI_WAVE_2, BUI_WAVE_3 };
    for (int band = 0; band < 3; band++) {
        for (int vx = 0; vx < BUI_V_W; vx++) {
            int idx = (((vx + phase[band]) * 64) / BUI_V_W) & 63;
            int vy = base[band] + (bui_sin64[idx] * amp[band]) / 127;
            bui_fill(vx, vy, 1, thick[band], cols[band]);
            bui_hline(vx, vy, 1, band == 2 ? BUI_CYAN : BUI_TEAL_DIM);
        }
    }
}

void bui_topbar(const char *mode) {
    bui_fill(0, 0, BUI_V_W, 34, BUI_BG);
    bui_hline(0, 33, BUI_V_W, BUI_BORDER);
    bui_vlogo(14, 8, 20, 18);
    bui_text("VQEAF OS", 40, 11, 2, BUI_TEXT);
    int tw = bui_text_w(mode, 1);
    bui_text(mode, bui_vwidth() - 16 - tw, 14, 1, BUI_DIM);
}

/* -------------------------------------------------------------------- icons */

static void ip(int x, int y, int w, int h, struct umos_color c) {
    bui_fill(x, y, w, h, c);
}

void bui_icon(int id, int x, int y, struct umos_color c) {
    switch (id) {
    case BUI_ICON_PLAY:                 /* right-pointing triangle */
        for (int row = 0; row < 11; row++) {
            int d = row <= 5 ? row : 10 - row;
            ip(x + 4, y + 2 + row, 1 + d, 1, c);
        }
        break;
    case BUI_ICON_REBOOT:               /* circular arrow + head */
        ip(x + 3, y + 2, 8, 1, c);
        ip(x + 2, y + 3, 1, 6, c);
        ip(x + 3, y + 10, 7, 1, c);
        ip(x + 11, y + 8, 1, 3, c);
        ip(x + 11, y + 3, 1, 2, c);
        ip(x + 12, y + 3, 2, 1, c);
        break;
    case BUI_ICON_DOWNLOAD:             /* arrow into a tray */
        ip(x + 7, y + 1, 2, 7, c);
        ip(x + 4, y + 5, 2, 2, c);
        ip(x + 10, y + 5, 2, 2, c);
        ip(x + 3, y + 4, 1, 2, c);
        ip(x + 12, y + 4, 1, 2, c);
        ip(x + 2, y + 12, 12, 1, c);
        ip(x + 2, y + 10, 1, 2, c);
        ip(x + 13, y + 10, 1, 2, c);
        break;
    case BUI_ICON_GEAR:                 /* blocky cog */
        ip(x + 6, y + 1, 4, 2, c);
        ip(x + 6, y + 13, 4, 2, c);
        ip(x + 1, y + 6, 2, 4, c);
        ip(x + 13, y + 6, 2, 4, c);
        ip(x + 4, y + 4, 8, 8, c);
        ip(x + 6, y + 6, 4, 4, BUI_SURFACE);
        break;
    case BUI_ICON_POWER:                /* broken ring + stem */
        ip(x + 3, y + 3, 1, 6, c);
        ip(x + 12, y + 3, 1, 6, c);
        ip(x + 4, y + 2, 2, 1, c);
        ip(x + 10, y + 2, 2, 1, c);
        ip(x + 4, y + 10, 2, 1, c);
        ip(x + 10, y + 10, 2, 1, c);
        ip(x + 7, y + 1, 2, 8, c);
        break;
    case BUI_ICON_DATABASE:             /* stacked cylinder */
        ip(x + 2, y + 2, 12, 3, c);
        ip(x + 2, y + 5, 12, 1, BUI_SURFACE);
        ip(x + 2, y + 6, 12, 3, c);
        ip(x + 2, y + 9, 12, 1, BUI_SURFACE);
        ip(x + 2, y + 10, 12, 3, c);
        ip(x + 2, y + 13, 12, 1, c);
        break;
    case BUI_ICON_TRASH:                /* bin + lid + ribs */
        ip(x + 6, y + 1, 4, 1, c);
        ip(x + 4, y + 2, 8, 1, c);
        ip(x + 3, y + 4, 1, 9, c);
        ip(x + 12, y + 4, 1, 9, c);
        ip(x + 3, y + 13, 10, 1, c);
        ip(x + 6, y + 6, 1, 5, c);
        ip(x + 9, y + 6, 1, 5, c);
        break;
    case BUI_ICON_BROOM:                /* brush, diagonal */
        for (int i = 0; i < 8; i++) ip(x + 5 + i, y + 9 - i, 2, 2, c);
        ip(x + 2, y + 9, 5, 5, c);
        ip(x + 3, y + 14, 1, 2, c);
        ip(x + 5, y + 14, 1, 2, c);
        break;
    case BUI_ICON_SATELLITE:            /* slanted body + two panels */
        for (int i = 0; i < 6; i++) ip(x + 5 + i, y + 9 - i, 2, 2, c);
        ip(x + 1, y + 2, 6, 2, c);
        ip(x + 9, y + 10, 6, 2, c);
        ip(x + 3, y + 4, 1, 1, c);
        ip(x + 11, y + 9, 1, 1, c);
        break;
    case BUI_ICON_INFO:                 /* circled i */
        ip(x + 5, y + 1, 6, 1, c);
        ip(x + 3, y + 2, 10, 1, c);
        ip(x + 2, y + 3, 1, 10, c);
        ip(x + 13, y + 3, 1, 10, c);
        ip(x + 3, y + 13, 10, 1, c);
        ip(x + 5, y + 14, 6, 1, c);
        ip(x + 7, y + 5, 2, 2, c);
        ip(x + 7, y + 8, 2, 4, c);
        break;
    case BUI_ICON_PHONE:                /* handset on the diagonal */
        ip(x + 3, y + 2, 3, 3, c);
        ip(x + 4, y + 4, 2, 2, c);
        for (int i = 0; i < 4; i++) ip(x + 5 + i, y + 6 + i, 2, 2, c);
        ip(x + 11, y + 9, 2, 2, c);
        ip(x + 11, y + 11, 3, 3, c);
        break;
    case BUI_ICON_MESSAGE:              /* speech bubble with tail */
        ip(x + 2, y + 3, 12, 8, c);
        ip(x + 4, y + 11, 3, 2, c);
        ip(x + 4, y + 13, 1, 1, c);
        break;
    case BUI_ICON_GLOBE:                /* meridian sphere */
        {
            int cx = x + 8, cy = y + 8, r = 6;
            for (int dy = -r; dy <= r; dy++) {
                int half = isqrt_i(r * r - dy * dy);
                ip(cx - half, cy + dy, 1, 1, c);
                ip(cx + half, cy + dy, 1, 1, c);
            }
            ip(cx - 5, cy - 3, 11, 1, c);
            ip(cx - 5, cy + 3, 11, 1, c);
            ip(cx - r, cy, 2 * r + 1, 1, c);
            ip(cx, cy - r, 1, 2 * r + 1, c);
        }
        break;
    case BUI_ICON_CAMERA:               /* outline body + lens */
        ip(x + 2, y + 5, 12, 1, c);
        ip(x + 2, y + 13, 12, 1, c);
        ip(x + 2, y + 5, 1, 9, c);
        ip(x + 13, y + 5, 1, 9, c);
        ip(x + 5, y + 3, 4, 2, c);
        ip(x + 6, y + 7, 4, 1, c);
        ip(x + 6, y + 10, 4, 1, c);
        ip(x + 6, y + 7, 1, 4, c);
        ip(x + 9, y + 7, 1, 4, c);
        break;
    case BUI_ICON_PHOTO:                /* two peaks + sun */
        ip(x + 2, y + 12, 12, 2, c);
        for (int i = 0; i < 6; i++) ip(x + 6 - i, y + 6 + i, 1 + 2 * i, 1, c);
        for (int i = 0; i < 4; i++) ip(x + 11 - i, y + 8 + i, 1 + 2 * i, 1, c);
        ip(x + 11, y + 3, 3, 3, c);
        break;
    case BUI_ICON_PIN:                  /* map pin with a hole */
        ip(x + 6, y + 1, 4, 1, c);
        ip(x + 5, y + 2, 2, 1, c);
        ip(x + 9, y + 2, 2, 1, c);
        ip(x + 4, y + 3, 2, 2, c);
        ip(x + 10, y + 3, 2, 2, c);
        ip(x + 4, y + 5, 2, 2, c);
        ip(x + 10, y + 5, 2, 2, c);
        ip(x + 5, y + 7, 2, 2, c);
        ip(x + 9, y + 7, 2, 2, c);
        ip(x + 6, y + 9, 2, 2, c);
        ip(x + 7, y + 11, 2, 3, c);
        ip(x + 7, y + 3, 2, 2, c);
        break;
    case BUI_ICON_MAIL:                 /* envelope outline + flap */
        ip(x + 2, y + 4, 12, 1, c);
        ip(x + 2, y + 11, 12, 1, c);
        ip(x + 2, y + 4, 1, 8, c);
        ip(x + 13, y + 4, 1, 8, c);
        for (int i = 0; i < 6; i++) {
            ip(x + 2 + i, y + 4 + i, 1, 1, c);
            ip(x + 13 - i, y + 4 + i, 1, 1, c);
        }
        break;
    case BUI_ICON_CLOCK:                /* dial + two hands */
        {
            int cx = x + 8, cy = y + 8, r = 6;
            for (int dy = -r; dy <= r; dy++) {
                int half = isqrt_i(r * r - dy * dy);
                ip(cx - half, cy + dy, 1, 1, c);
                ip(cx + half, cy + dy, 1, 1, c);
            }
            ip(cx, cy - 4, 1, 5, c);
            ip(cx, cy, 4, 1, c);
        }
        break;
    case BUI_ICON_CALENDAR:             /* sheet, header rule, two days */
        ip(x + 2, y + 3, 12, 1, c);
        ip(x + 2, y + 13, 12, 1, c);
        ip(x + 2, y + 3, 1, 11, c);
        ip(x + 13, y + 3, 1, 11, c);
        ip(x + 2, y + 6, 12, 1, c);
        ip(x + 4, y + 1, 1, 2, c);
        ip(x + 11, y + 1, 1, 2, c);
        ip(x + 5, y + 9, 2, 2, c);
        ip(x + 9, y + 9, 2, 2, c);
        break;
    case BUI_ICON_CALC:                 /* display + six keys */
        ip(x + 3, y + 2, 10, 1, c);
        ip(x + 3, y + 13, 10, 1, c);
        ip(x + 3, y + 2, 1, 12, c);
        ip(x + 12, y + 2, 1, 12, c);
        ip(x + 5, y + 4, 6, 2, c);
        ip(x + 5, y + 8, 1, 1, c);
        ip(x + 7, y + 8, 1, 1, c);
        ip(x + 9, y + 8, 1, 1, c);
        ip(x + 5, y + 11, 1, 1, c);
        ip(x + 7, y + 11, 1, 1, c);
        ip(x + 9, y + 11, 1, 1, c);
        break;
    case BUI_ICON_FOLDER:               /* tab + body */
        ip(x + 2, y + 4, 5, 2, c);
        ip(x + 2, y + 6, 12, 7, c);
        break;
    case BUI_ICON_SUN:                  /* disc + eight rays */
        ip(x + 6, y + 6, 4, 4, c);
        ip(x + 7, y + 1, 2, 2, c);
        ip(x + 7, y + 13, 2, 2, c);
        ip(x + 1, y + 7, 2, 2, c);
        ip(x + 13, y + 7, 2, 2, c);
        ip(x + 3, y + 3, 1, 1, c);
        ip(x + 12, y + 3, 1, 1, c);
        ip(x + 3, y + 12, 1, 1, c);
        ip(x + 12, y + 12, 1, 1, c);
        break;
    case BUI_ICON_CONTACTS:             /* head + shoulders */
        ip(x + 6, y + 2, 4, 1, c);
        ip(x + 5, y + 3, 6, 1, c);
        ip(x + 5, y + 4, 6, 2, c);
        ip(x + 4, y + 8, 8, 1, c);
        ip(x + 3, y + 9, 10, 2, c);
        ip(x + 2, y + 11, 12, 2, c);
        break;
    case BUI_ICON_MIC:                  /* capsule + stand */
        ip(x + 6, y + 2, 4, 7, c);
        ip(x + 5, y + 3, 1, 5, c);
        ip(x + 10, y + 3, 1, 5, c);
        ip(x + 4, y + 9, 1, 2, c);
        ip(x + 11, y + 9, 1, 2, c);
        ip(x + 5, y + 11, 6, 1, c);
        ip(x + 7, y + 12, 2, 2, c);
        break;
    case BUI_ICON_TERMINAL:             /* prompt frame */
        ip(x + 1, y + 3, 14, 1, c);
        ip(x + 1, y + 12, 14, 1, c);
        ip(x + 1, y + 3, 1, 10, c);
        ip(x + 14, y + 3, 1, 10, c);
        ip(x + 4, y + 6, 1, 1, c);
        ip(x + 5, y + 7, 1, 1, c);
        ip(x + 4, y + 8, 1, 1, c);
        ip(x + 8, y + 9, 4, 1, c);
        break;
    case BUI_ICON_SEARCH:               /* ring + handle */
        {
            int cx = x + 7, cy = y + 7, r = 4;
            for (int dy = -r; dy <= r; dy++) {
                int half = isqrt_i(r * r - dy * dy);
                ip(cx - half, cy + dy, 1, 1, c);
                ip(cx + half, cy + dy, 1, 1, c);
            }
            for (int i = 0; i < 4; i++) ip(cx + 3 + i, cy + 3 + i, 2, 2, c);
        }
        break;
    case BUI_ICON_WIFI:                 /* three arcs + dot */
        ip(x + 7, y + 3, 2, 2, c);
        ip(x + 5, y + 5, 6, 1, c);
        ip(x + 3, y + 6, 2, 1, c);
        ip(x + 11, y + 6, 2, 1, c);
        ip(x + 4, y + 8, 8, 1, c);
        ip(x + 1, y + 9, 3, 1, c);
        ip(x + 12, y + 9, 3, 1, c);
        ip(x + 2, y + 11, 12, 1, c);
        ip(x, y + 12, 2, 1, c);
        ip(x + 14, y + 12, 2, 1, c);
        break;
    case BUI_ICON_SIGNAL:               /* four bars */
        ip(x + 2, y + 10, 2, 3, c);
        ip(x + 5, y + 8, 2, 5, c);
        ip(x + 8, y + 5, 2, 8, c);
        ip(x + 11, y + 2, 2, 11, c);
        break;
    case BUI_ICON_LOCK:                 /* shackle + body */
        ip(x + 5, y + 2, 6, 1, c);
        ip(x + 4, y + 3, 1, 3, c);
        ip(x + 11, y + 3, 1, 3, c);
        ip(x + 3, y + 6, 10, 8, c);
        break;
    case BUI_ICON_MUSIC:                /* quaver */
        ip(x + 6, y + 3, 1, 8, c);
        ip(x + 7, y + 3, 3, 1, c);
        ip(x + 10, y + 3, 1, 4, c);
        ip(x + 4, y + 10, 4, 3, c);
        break;
    case BUI_ICON_STORE:                /* shopping bag */
        ip(x + 3, y + 6, 10, 7, c);
        ip(x + 4, y + 4, 1, 2, c);
        ip(x + 11, y + 4, 1, 2, c);
        ip(x + 5, y + 3, 1, 1, c);
        ip(x + 6, y + 2, 4, 1, c);
        ip(x + 10, y + 3, 1, 1, c);
        break;
    case BUI_ICON_NOTES:                /* ruled page */
        ip(x + 3, y + 3, 10, 1, c);
        ip(x + 3, y + 6, 10, 1, c);
        ip(x + 3, y + 9, 10, 1, c);
        ip(x + 3, y + 12, 6, 1, c);
        break;
    case BUI_ICON_FLASH:                /* torch */
        ip(x + 5, y + 2, 6, 3, c);
        ip(x + 4, y + 5, 8, 2, c);
        ip(x + 6, y + 7, 4, 2, c);
        ip(x + 7, y + 9, 2, 6, c);
        break;
    case BUI_ICON_MONITOR:              /* display on a stand */
        ip(x + 1, y + 2, 14, 1, c);
        ip(x + 1, y + 11, 14, 1, c);
        ip(x + 1, y + 2, 1, 10, c);
        ip(x + 14, y + 2, 1, 10, c);
        ip(x + 6, y + 13, 4, 1, c);
        ip(x + 7, y + 12, 2, 1, c);
        break;
    default:
        break;
    }
}

void bui_chevron(int x, int y, struct umos_color c) {
    for (int i = 0; i < 5; i++) {
        ip(x + i, y + 2 + i, 1, 1, c);
        ip(x + i, y + 10 - i, 1, 1, c);
    }
}

/* Two strokes, no font glyph: a 12x12 Material check mark. */
void bui_check(int x, int y, struct umos_color c) {
    for (int i = 0; i < 4; i++) ip(x + i, y + 5 + i, 2, 2, c);
    for (int i = 0; i < 7; i++) ip(x + 4 + i, y + 8 - i, 2, 2, c);
}

/* ---- system bar ----------------------------------------------------------- */

void bui_battery(int x, int y, int pct, struct umos_color c) {
    ip(x + 1, y + 3, 11, 1, c);
    ip(x + 1, y + 10, 11, 1, c);
    ip(x + 1, y + 3, 1, 8, c);
    ip(x + 11, y + 3, 1, 8, c);
    ip(x + 12, y + 5, 1, 4, c);
    if (pct > 0) {
        int w = (9 * pct) / 100;
        if (w > 9) w = 9;
        ip(x + 2, y + 4, w, 6, c);
    }
}

void bui_statusbar(const char *label, int battery_pct) {
    if (label) bui_text(label, 12, 6, 2, BUI_TEXT);
    bui_battery(bui_vwidth() - 32, 5, battery_pct, BUI_TEXT);
}

void bui_confirm(const char *action, int cancel_selected) {
    bui_fill(0, 0, BUI_V_W, BUI_V_H, BUI_BG_DEEP);
    bui_round(BUI_DLG_X, BUI_DLG_Y, BUI_DLG_W, BUI_DLG_H, 14,
              BUI_SURFACE, BUI_TEAL);
    bui_text("CONFIRM ACTION", BUI_DLG_X + 16, BUI_DLG_Y + 18, 2, BUI_TEXT);
    bui_hline(BUI_DLG_X + 16, BUI_DLG_Y + 44, BUI_DLG_W - 32, BUI_BORDER);
    bui_text(action, BUI_DLG_X + 16, BUI_DLG_Y + 56, 1, BUI_DANGER);
    bui_text("The GUI only queues an intent.", BUI_DLG_X + 16,
             BUI_DLG_Y + 76, 1, BUI_TEXT);
    bui_text("Nothing is written until an", BUI_DLG_X + 16,
             BUI_DLG_Y + 92, 1, BUI_TEXT);
    bui_text("executor is bound to it.", BUI_DLG_X + 16,
             BUI_DLG_Y + 106, 1, BUI_TEXT);
    bui_text("CANCEL is the safe default.", BUI_DLG_X + 16,
             BUI_DLG_Y + 126, 1, BUI_DIM);

    int cancel_on = (cancel_selected == 0);
    int accept_on = !cancel_on;
    bui_round(BUI_BTN_C_X, BUI_BTN_Y, BUI_BTN_W, BUI_BTN_H, 8,
              cancel_on ? BUI_SURFACE_HI : BUI_SURFACE,
              cancel_on ? BUI_TEAL : BUI_BORDER);
    bui_text_center("CANCEL", BUI_BTN_C_X + BUI_BTN_W / 2,
                    BUI_BTN_Y + 14, 2, cancel_on ? BUI_TEXT : BUI_DIM);
    bui_round(BUI_BTN_A_X, BUI_BTN_Y, BUI_BTN_W, BUI_BTN_H, 8,
              accept_on ? BUI_DANGER : BUI_SURFACE,
              accept_on ? BUI_DANGER : BUI_BORDER);
    bui_text_center("CONFIRM", BUI_BTN_A_X + BUI_BTN_W / 2,
                    BUI_BTN_Y + 14, 2, accept_on ? BUI_TEXT : BUI_DIM);

    bui_text_center("VOL : SWITCH    POWER : SELECT    BACK : ABORT",
                    180, BUI_DLG_Y + BUI_DLG_H + 14, 1, BUI_DIM);
}
