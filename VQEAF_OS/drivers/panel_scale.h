/* SPDX-License-Identifier: MIT */
/* platform/violet/panel_scale.h - 800x480 UI onto the 1080x2340 panel.
 *
 * The scaler is a pure function of the UI buffer, so the exact letterbox and
 * rotation maths can be measured on the host (tests/panel_scale_test.c)
 * without a phone or a DRM device.
 */
#ifndef HOMO_PANEL_SCALE_H
#define HOMO_PANEL_SCALE_H

#include <stdint.h>

#define VIO_PANEL_MAP_MAX 2400

typedef struct {
    int x1, y1, x2, y2; /* inclusive */
} vio_rect_t;

typedef struct {
    int ui_w, ui_h;
    int panel_w, panel_h;
    int rotation;       /* 0, 90, 180 or 270 degrees clockwise */
    int lw, lh;         /* logical panel size after rotation */
    int scale_q16;      /* UI pixel -> logical pixel, Q16 */
    int offset_x, offset_y;
    int out_w, out_h;   /* scaled UI size inside the logical panel */
    int stride;         /* scanout pixels per row (DRM pitch / 4) */
    int16_t map_lx[VIO_PANEL_MAP_MAX];
    int16_t map_ly[VIO_PANEL_MAP_MAX];
} vio_panel_t;

void vio_panel_init(vio_panel_t *panel, int ui_w, int ui_h,
                    int panel_w, int panel_h, int rotation);
void vio_panel_set_rotation(vio_panel_t *panel, int rotation);

/* Re-points the scaler at a different UI canvas (540x1170 <-> 800x480) and
 * rebuilds the sample maps for the current rotation. */
void vio_panel_set_ui_size(vio_panel_t *panel, int ui_w, int ui_h);

/* The DRM dumb buffer is not always tightly packed: on violet a 1080-pixel
 * row has a 4352-byte pitch, so every row carries 8 pixels of padding. The
 * renderer must be told the real stride or the image is sheared by 8 pixels
 * per row. The default after vio_panel_init is panel_w (packed). */
void vio_panel_set_stride(vio_panel_t *panel, int stride);

vio_rect_t vio_panel_full_rect(const vio_panel_t *panel);

/* Renders `rect` (physical panel coordinates) from the UI buffer into an
 * XRGB8888 scanout buffer. Pixels outside the scaled UI are black. */
void vio_panel_render(const vio_panel_t *panel, const uint16_t *ui,
                      uint32_t *scanout, vio_rect_t rect);

/* Raw touch (native portrait panel coordinates) -> UI coordinates.
 * Returns 0 on a hit, -1 when the point falls in the letterbox. */
int vio_panel_touch_map(const vio_panel_t *panel, int raw_x, int raw_y,
                        int raw_w, int raw_h, int *ui_x, int *ui_y);

/* Maps an LVGL invalidated area (UI coordinates) to the physical panel
 * rectangle that must be repainted. */
vio_rect_t vio_panel_map_ui_rect(const vio_panel_t *panel, vio_rect_t ui);

#endif /* HOMO_PANEL_SCALE_H */
