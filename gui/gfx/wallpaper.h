/* SPDX-License-Identifier: MIT */
/* gui/gfx/wallpaper.h - the backdrop layer the shell zooms and blurs.
 *
 * The scene is generated from integers only (gradient, aurora ribbons, ridge
 * noise, stars) into three progressively smaller and softer source levels, and
 * the compositor stretches one of them over the panel with a zoom factor. No
 * decoded image, no third-party asset: everything on the glass is drawn here.
 */

#ifndef UMOS_WALLPAPER_H
#define UMOS_WALLPAPER_H

#include <stdint.h>

/* Source levels, each about half the linear size of the one above. The panel
 * is 1080x2340, so L0 is a quarter-scale render and L2 is a 1/16-scale one;
 * sampling the soft levels is what makes the blur cheap. */
#define WALL_L0_W 270
#define WALL_L0_H 585
#define WALL_L1_W 135
#define WALL_L1_H 293
#define WALL_L2_W 68
#define WALL_L2_H 147

/* q8 zoom: 256 maps the source exactly onto the panel. */
#define WALL_ZOOM_BASE 256

/* Draws the scene into the three levels. Idempotent: a second call is free
 * unless a caller asks for a rebuild. */
void wallpaper_build(void);
void wallpaper_invalidate(void);
int wallpaper_ready(void);

/* Paints the whole panel. zoom_q >= 256, blur_q and scrim_q in 0..256.
 * Returns the number of destination pixels written, which is what the gate
 * measures instead of a wall clock. */
uint32_t wallpaper_composite(int zoom_q, int blur_q, int scrim_q);

/* The last composite's parameters, for the gate's readback. */
int wallpaper_last_zoom(void);
int wallpaper_last_blur(void);
int wallpaper_last_scrim(void);

/* Destination pixels the last wallpaper_composite() wrote. The gate uses this
 * as the cost figure: one frame of a full-panel composite is exactly
 * gfx_width()*gfx_height() pixels, and anything less says the blit clipped. */
uint32_t wallpaper_pixels_last_composited(void);

/* Material You style source: samples the generated L0 scene at a spread of
 * points and returns up to `cap` distinct RGB565 colours. Building the wall
 * is the caller's job; this never invents a colour that is not in the scene.
 * Returns the number of samples written. */
int wallpaper_sample_palette(uint16_t *out, int cap);

#endif
