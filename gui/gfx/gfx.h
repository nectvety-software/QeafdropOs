/* SPDX-License-Identifier: MIT */
/* gui/gfx/gfx.h - Basic graphics primitives */

#ifndef UMOS_GFX_H
#define UMOS_GFX_H

#include <stdint.h>

struct umos_rect {
    int16_t x, y;
    uint16_t w, h;
};

struct umos_color {
    uint8_t r, g, b, a;
};

/* Panel rotation. Drawing always happens in LOGICAL coordinates; every pixel
 * is mapped to the physical portrait framebuffer (1080x2340 on violet). At
 * 90/270 the logical surface swaps to 2340x1080, so a landscape layout has
 * the full width of the panel to work with. No second framebuffer is needed.
 *
 *   ROT_0   logical (1080,2340) -> physical (x, y)
 *   ROT_90  logical (2340,1080) -> physical (y, 2339-x)
 *   ROT_180 logical (1080,2340) -> physical (1079-x, 2339-y)
 *   ROT_270 logical (2340,1080) -> physical (1079-y, x)
 */
enum gfx_rotation {
    GFX_ROT_0 = 0,
    GFX_ROT_90,
    GFX_ROT_180,
    GFX_ROT_270,
    GFX_ROT_COUNT
};

void gfx_set_rotation(enum gfx_rotation r);
enum gfx_rotation gfx_rotation(void);
const char *gfx_rotation_name(enum gfx_rotation r);
uint32_t gfx_logical_width(void);
uint32_t gfx_logical_height(void);
/* One logical pixel, already packed as RGB565. All primitives and the font
 * funnel through here so a rotated panel sees exactly the same drawing. */
void gfx_plot565(int32_t lx, int32_t ly, uint16_t col);
/* Inverse map for input: physical touch -> logical drawing coordinates. */
void gfx_map_touch(int32_t px, int32_t py, int32_t *lx, int32_t *ly);

/* Clip in LOGICAL coordinates. A split pane or a floating window draws its
 * whole screen into one rect: every primitive and the font are clipped here,
 * so a full-screen bui_begin() cannot repaint its neighbour's half. */
void gfx_set_clip(int32_t x, int32_t y, int32_t w, int32_t h);
void gfx_clip_reset(void);
void gfx_get_clip(int32_t *x, int32_t *y, int32_t *w, int32_t *h);

void gfx_init(uint16_t *fb, uint32_t w, uint32_t h, uint32_t stride);
void gfx_pixel(int16_t x, int16_t y, struct umos_color c);
void gfx_fill_rect(const struct umos_rect *r, struct umos_color c);
void gfx_draw_rect(const struct umos_rect *r, struct umos_color c);
/* Solid colour laid over whatever is already in the rect. alpha_q is q8:
 * 0 leaves the backdrop untouched, 256 behaves like gfx_fill_rect. The
 * translucent sheets that cover the blurred wallpaper run through here. */
void gfx_blend_rect(const struct umos_rect *r, struct umos_color c, int alpha_q);
void gfx_flush(int x, int y, int w, int h);
uint32_t gfx_width(void);
uint32_t gfx_height(void);
uint32_t gfx_stride(void);
uint16_t *gfx_fb(void);

#endif
