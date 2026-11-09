/* SPDX-License-Identifier: MIT */
/* gui/gfx/wallpaper.c - procedural backdrop + the zoom/blur blitter.
 *
 * Integer arithmetic only, no libm, no decoded image. The scene is drawn once
 * into a quarter-scale buffer, then reduced and blurred into two softer
 * levels; a frame costs one pass over the panel that samples two of those
 * levels. Sampling a *small* source is what keeps a full-screen zoom cheap:
 * the soft levels are 79 KB and 20 KB, so they stay in cache while the panel
 * is written.
 */

#include "wallpaper.h"
#include "gfx.h"
#include <stddef.h>

static uint16_t l0[WALL_L0_W * WALL_L0_H];
static uint16_t l1[WALL_L1_W * WALL_L1_H];
static uint16_t l2[WALL_L2_W * WALL_L2_H];

static int g_ready;
static int g_zoom, g_blur, g_scrim;
static uint32_t g_px;

/* The backdrop keeps its own scene: near-black navy, teal and cyan. The UI
 * chrome above it now uses the neutral Material palette, and the accent ramp
 * is still derived from these pixels - that is the dynamic-colour source. */
static const int SKY_TOP_R = 4,  SKY_TOP_G = 10, SKY_TOP_B = 18;
static const int SKY_LOW_R = 10, SKY_LOW_G = 22, SKY_LOW_B = 32;
static const int AUR_R = 16, AUR_G = 224, AUR_B = 200;
static const int CYA_R = 40, CYA_G = 214, CYA_B = 245;
static const int RIDGE_FAR = 0, RIDGE_MID = 1, RIDGE_NEAR = 2;

static uint16_t pack565(int r, int g, int b)
{
    if (r < 0) r = 0; if (r > 255) r = 255;
    if (g < 0) g = 0; if (g > 255) g = 255;
    if (b < 0) b = 0; if (b > 255) b = 255;
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

static void unpack565(uint16_t v, int *r, int *g, int *b)
{
    *r = (v >> 11) * 255 / 31;
    *g = ((v >> 5) & 63) * 255 / 63;
    *b = (v & 31) * 255 / 31;
}

/* Deterministic hash noise: the scene must repeat exactly on every boot and
 * on every host, or a gate could not compare two frames of it. */
static unsigned hash1(unsigned x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

/* Triangle wave in 0..1024, integer stand-in for a sine. */
static int tri(int t)
{
    int p = t % 2048;
    if (p < 0) p += 2048;
    return p < 1024 ? p : 2048 - p;
}

static void l0_put(int x, int y, uint16_t v)
{
    if (x < 0 || y < 0 || x >= WALL_L0_W || y >= WALL_L0_H) return;
    l0[y * WALL_L0_W + x] = v;
}

static uint16_t l0_at(int x, int y)
{
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= WALL_L0_W) x = WALL_L0_W - 1;
    if (y >= WALL_L0_H) y = WALL_L0_H - 1;
    return l0[y * WALL_L0_W + x];
}

/* Ridge height at x for one of the three ranges, from hashed control points
 * interpolated linearly across the width. The two endpoints are signed on
 * purpose: `b - a` is negative two thirds of the time, and letting it wrap
 * through unsigned produced a ridge top far outside the panel - which on the
 * host is a wild loop and, once the optimiser takes the signed-overflow free
 * pass, a crash rather than a bad picture. */
static int ridge_y(int x, int band, int seed)
{
    const int span = 30;
    int i0 = x / span;
    int f = x % span;
    int a = (int)(hash1((unsigned)(i0 + seed * 977)) % 1024U);
    int b = (int)(hash1((unsigned)(i0 + 1 + seed * 977)) % 1024U);
    int h = a + ((b - a) * f) / span;                 /* 0..1023 */
    int base = band == RIDGE_FAR ? 300 : (band == RIDGE_MID ? 360 : 430);
    int amp = band == RIDGE_FAR ? 90 : (band == RIDGE_MID ? 70 : 50);
    return base + (h * amp) / 1024;
}

static void draw_sky(void)
{
    for (int y = 0; y < WALL_L0_H; y++) {
        int k = (y * 256) / (WALL_L0_H - 1);
        int r = SKY_TOP_R + ((SKY_LOW_R - SKY_TOP_R) * k) / 256;
        int g = SKY_TOP_G + ((SKY_LOW_G - SKY_TOP_G) * k) / 256;
        int b = SKY_TOP_B + ((SKY_LOW_B - SKY_TOP_B) * k) / 256;
        uint16_t v = pack565(r, g, b);
        for (int x = 0; x < WALL_L0_W; x++) l0_put(x, y, v);
    }
}

static void draw_stars(void)
{
    for (int i = 0; i < 900; i++) {
        unsigned h = hash1((unsigned)i * 2654435761U);
        int x = (int)(h % (unsigned)WALL_L0_W);
        int y = (int)((h >> 9) % 260U);
        int b = 90 + (int)((h >> 17) % 140U);
        uint16_t old = l0_at(x, y);
        int r, g, bl;
        unpack565(old, &r, &g, &bl);
        l0_put(x, y, pack565(r + b / 3, g + b / 3, bl + b));
    }
}

/* Three ribbons: a centre line that wanders with triangle waves, and a
 * vertical falloff. Drawn additively so overlaps glow brighter, which is the
 * one thing that makes an aurora read as an aurora. */
static void draw_aurora(void)
{
    static const int yc[3] = { 96, 150, 70 };
    static const int amp[3] = { 34, 22, 44 };
    static const int per[3] = { 640, 900, 480 };
    static const int thick[3] = { 26, 18, 14 };

    for (int band = 0; band < 3; band++) {
        for (int x = 0; x < WALL_L0_W; x++) {
            int c = yc[band] + (tri(x * 2048 / per[band]) - 512) * amp[band] / 512;
            for (int dy = -thick[band]; dy <= thick[band]; dy++) {
                int y = c + dy;
                if (y < 0 || y >= WALL_L0_H) continue;
                int fall = (thick[band] - (dy < 0 ? -dy : dy)) * 256 / (thick[band] + 1);
                int shimmer = 200 + (tri(x * 4 + band * 700) % 120);
                int k = (fall * shimmer) / 512;
                if (k <= 0) continue;
                int r0, g0, b0;
                unpack565(l0_at(x, y), &r0, &g0, &b0);
                if (band == 1)
                    l0_put(x, y, pack565(r0 + (CYA_R - r0) * k / 320,
                                         g0 + (CYA_G - g0) * k / 320,
                                         b0 + (CYA_B - b0) * k / 320));
                else
                    l0_put(x, y, pack565(r0 + (AUR_R - r0) * k / 320,
                                         g0 + (AUR_G - g0) * k / 320,
                                         b0 + (AUR_B - b0) * k / 320));
            }
        }
    }
}

static void draw_ridges(void)
{
    static const int tone[3][3] = {
        { 8, 26, 34 }, { 6, 20, 28 }, { 3, 12, 18 },
    };

    for (int band = RIDGE_FAR; band <= RIDGE_NEAR; band++) {
        for (int x = 0; x < WALL_L0_W; x++) {
            int top = ridge_y(x, band, band + 1);
            for (int y = top; y < WALL_L0_H; y++) {
                int d = y - top;
                int shade = 256 - (d > 120 ? 120 : d) * 256 / 160;
                if (shade < 96) shade = 96;
                l0_put(x, y, pack565(tone[band][0] * shade / 256,
                                     tone[band][1] * shade / 256,
                                     tone[band][2] * shade / 256));
            }
            /* crest line, one pixel of the ridge's own light */
            l0_put(x, top, pack565(tone[band][0] + 24, tone[band][1] + 70,
                                   tone[band][2] + 66));
        }
    }
}

/* 2x2 average onto the next level down. */
static void reduce(const uint16_t *src, int sw, int sh, uint16_t *dst,
                   int dw, int dh)
{
    for (int y = 0; y < dh; y++) {
        for (int x = 0; x < dw; x++) {
            int r = 0, g = 0, b = 0, n = 0;
            for (int j = 0; j < 2; j++) {
                int sy = y * 2 + j;
                if (sy >= sh) continue;
                for (int i = 0; i < 2; i++) {
                    int sx = x * 2 + i;
                    int rr, gg, bb;
                    if (sx >= sw) continue;
                    unpack565(src[sy * sw + sx], &rr, &gg, &bb);
                    r += rr; g += gg; b += bb; n++;
                }
            }
            if (!n) n = 1;
            dst[y * dw + x] = pack565(r / n, g / n, b / n);
        }
    }
}

/* Separable [1 2 1] blur; passes is how many times it runs. */
static void blur(uint16_t *buf, int w, int h, int passes)
{
    static uint16_t tmp[WALL_L1_W * WALL_L1_H];
    int r, g, b, rl, gl, bl, rr, gr, br;

    for (int p = 0; p < passes; p++) {
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                int xl = x > 0 ? x - 1 : x;
                int xr = x < w - 1 ? x + 1 : x;
                unpack565(buf[y * w + xl], &rl, &gl, &bl);
                unpack565(buf[y * w + xr], &rr, &gr, &br);
                unpack565(buf[y * w + x], &r, &g, &b);
                tmp[y * w + x] = pack565((r * 2 + rl + rr) / 4,
                                         (g * 2 + gl + gr) / 4,
                                         (b * 2 + bl + br) / 4);
            }
        }
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) buf[y * w + x] = tmp[y * w + x];

        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                int yt = y > 0 ? y - 1 : y;
                int yb = y < h - 1 ? y + 1 : y;
                unpack565(buf[yt * w + x], &rl, &gl, &bl);
                unpack565(buf[yb * w + x], &rr, &gr, &br);
                unpack565(buf[y * w + x], &r, &g, &b);
                buf[y * w + x] = pack565((r * 2 + rl + rr) / 4,
                                         (g * 2 + gl + gr) / 4,
                                         (b * 2 + bl + br) / 4);
            }
        }
    }
}

static void build_levels(void)
{
    draw_sky();
    draw_stars();
    draw_aurora();
    draw_ridges();

    reduce(l0, WALL_L0_W, WALL_L0_H, l1, WALL_L1_W, WALL_L1_H);
    reduce(l1, WALL_L1_W, WALL_L1_H, l2, WALL_L2_W, WALL_L2_H);
    blur(l1, WALL_L1_W, WALL_L1_H, 1);
    blur(l2, WALL_L2_W, WALL_L2_H, 2);
}

void wallpaper_build(void)
{
    if (g_ready) return;
    build_levels();
    g_ready = 1;
}

void wallpaper_invalidate(void) { g_ready = 0; }
int wallpaper_ready(void) { return g_ready; }

int wallpaper_last_zoom(void) { return g_zoom; }
int wallpaper_last_blur(void) { return g_blur; }
int wallpaper_last_scrim(void) { return g_scrim; }

/* Bilinear read of one level at q8 source coordinates, plus the two mixes a
 * frame needs. All of it works on the packed 565 lanes instead of expanding to
 * 8 bits: a panel-wide pass is 2.5 million pixels, and the version that
 * unpacked each tap with `* 255 / 31` and repacked through three clamps measured
 * 99 ms per frame on the host, which is a slideshow. This form is a multiply,
 * an add and a shift per lane - and because 31*256 and 63*256 still fit their
 * own lanes, no field can bleed into its neighbour. */
static uint16_t lerp565(uint16_t a, uint16_t b, int t)
{
    unsigned u = (unsigned)t, v = 256u - u;
    unsigned ar = (a >> 11) & 31u, ag = (a >> 5) & 63u, ab = a & 31u;
    unsigned br = (b >> 11) & 31u, bg = (b >> 5) & 63u, bb = b & 31u;

    return (uint16_t)((((ar * v + br * u) >> 8) << 11) |
                      (((ag * v + bg * u) >> 8) << 5) |
                      ((ab * v + bb * u) >> 8));
}

static uint16_t sample_row(const uint16_t *r0, const uint16_t *r1, int w,
                           int sx_q8, int fy)
{
    int x0 = sx_q8 >> 8;
    int fx = sx_q8 & 255;
    int x1;

    if (x0 < 0) { x0 = 0; fx = 0; }
    if (x0 >= w) { x0 = w - 1; fx = 0; }
    x1 = x0 + 1 < w ? x0 + 1 : x0;
    return lerp565(lerp565(r0[x0], r0[x1], fx), lerp565(r1[x0], r1[x1], fx), fy);
}

uint32_t wallpaper_composite(int zoom_q, int blur_q, int scrim_q)
{
    const uint32_t w = gfx_width(), h = gfx_height();
    uint16_t *fb = gfx_fb();
    uint32_t stride_px;
    const uint16_t *a, *b;
    uint16_t scrim_col;
    int aw, ah, bw, bh, mix;
    int64_t du_q24, dv_q24, u_start;
    uint32_t px = 0;

    if (!fb || w < 2 || h < 2) return 0;
    wallpaper_build();
    stride_px = gfx_stride() / 2;

    if (zoom_q < WALL_ZOOM_BASE) zoom_q = WALL_ZOOM_BASE;
    if (blur_q < 0) blur_q = 0;
    if (blur_q > 256) blur_q = 256;
    if (scrim_q < 0) scrim_q = 0;
    if (scrim_q > 256) scrim_q = 256;

    /* blur_q walks L0 -> L1 -> L2; the two levels it sits between are mixed.
     * At either end only one level is read, which is the common case: home is
     * sharp and the cover state is fully blurred, so a settled backdrop costs
     * one tap per pixel instead of two. */
    if (blur_q <= 128) {
        a = l0; aw = WALL_L0_W; ah = WALL_L0_H;
        b = l1; bw = WALL_L1_W; bh = WALL_L1_H;
        mix = blur_q * 2;
    } else {
        a = l1; aw = WALL_L1_W; ah = WALL_L1_H;
        b = l2; bw = WALL_L2_W; bh = WALL_L2_H;
        mix = (blur_q - 128) * 2;
    }
    scrim_col = pack565(SKY_TOP_R, SKY_TOP_G, SKY_TOP_B);

    /* normalised span across the panel, q24 accumulator, zoom shrinks it */
    du_q24 = ((int64_t)65536 * 256 / zoom_q) * 256 / (int64_t)(w - 1);
    dv_q24 = ((int64_t)65536 * 256 / zoom_q) * 256 / (int64_t)(h - 1);
    u_start = 32768LL * 256 - (int64_t)(w - 1) * du_q24 / 2;

    for (uint32_t y = 0; y < h; y++) {
        int64_t v_q24 = 32768LL * 256 + ((int64_t)y - (int64_t)(h - 1) / 2) * dv_q24;
        int64_t sy_a = (v_q24 >> 8) * (ah - 1) >> 8;
        int64_t sy_b = (v_q24 >> 8) * (bh - 1) >> 8;
        int ya = (int)(sy_a >> 8), fy_a = (int)(sy_a & 255);
        int yb = (int)(sy_b >> 8), fy_b = (int)(sy_b & 255);
        const uint16_t *a0, *a1, *b0, *b1;
        uint16_t *row = fb + (size_t)y * stride_px;
        int64_t uu = u_start;

        if (ya < 0) { ya = 0; fy_a = 0; }
        if (ya >= ah) { ya = ah - 1; fy_a = 0; }
        if (yb < 0) { yb = 0; fy_b = 0; }
        if (yb >= bh) { yb = bh - 1; fy_b = 0; }
        a0 = a + (size_t)ya * aw;
        a1 = a + (size_t)(ya + 1 < ah ? ya + 1 : ya) * aw;
        b0 = b + (size_t)yb * bw;
        b1 = b + (size_t)(yb + 1 < bh ? yb + 1 : yb) * bw;

        for (uint32_t x = 0; x < w; x++) {
            int u16 = (int)(uu >> 8);
            uint16_t out;

            if (mix <= 0) {
                out = sample_row(a0, a1, aw, (u16 * (aw - 1)) >> 8, fy_a);
            } else if (mix >= 256) {
                out = sample_row(b0, b1, bw, (u16 * (bw - 1)) >> 8, fy_b);
            } else {
                uint16_t va = sample_row(a0, a1, aw, (u16 * (aw - 1)) >> 8, fy_a);
                uint16_t vb = sample_row(b0, b1, bw, (u16 * (bw - 1)) >> 8, fy_b);
                out = lerp565(va, vb, mix);
            }
            if (scrim_q > 0) out = lerp565(out, scrim_col, scrim_q);

            row[x] = out;
            px++;
            uu += du_q24;
        }
    }

    g_zoom = zoom_q;
    g_blur = blur_q;
    g_scrim = scrim_q;
    g_px = px;
    return px;
}

uint32_t wallpaper_pixels_last_composited(void) { return g_px; }

int wallpaper_sample_palette(uint16_t *out, int cap) {
    /* A spread across the aurora, the sky and the ridges. Integer-only
     * sampling: these are real pixels of the generated scene. */
    static const int pts[][2] = {
        { 20,  30}, {135,  60}, {250,  90},
        { 40, 220}, {135, 260}, {240, 300},
        { 25, 430}, {135, 470}, {250, 520},
        { 70, 560}, {200, 150}, {180, 380},
    };
    int n = 0;

    if (!out || cap <= 0) return 0;
    if (!wallpaper_ready()) wallpaper_build();
    for (unsigned i = 0; i < sizeof(pts) / sizeof(pts[0]) && n < cap; i++) {
        uint16_t v = l0_at(pts[i][0], pts[i][1]);
        int dup = 0;
        for (int j = 0; j < n; j++) if (out[j] == v) { dup = 1; break; }
        if (!dup) out[n++] = v;
    }
    return n;
}
