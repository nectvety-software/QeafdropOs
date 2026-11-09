/* SPDX-License-Identifier: MIT */
/* gui/compositor.h - the layer that decides what the panel looks like between
 * screens.
 *
 * The shell has one backdrop (gui/gfx/wallpaper.c) and one set of numbers that
 * describe it: how far in it is zoomed, how blurred, how far down it is dimmed.
 * Opening a window moves those three numbers; every frame draws the backdrop
 * with the current values and then lets the window paint on top. That is the
 * whole model, which is what makes the transition cheap enough to run at the
 * panel's own rate.
 */

#ifndef UMOS_COMPOSITOR_H
#define UMOS_COMPOSITOR_H

#include <stdint.h>

/* q8 zoom. 256 maps the scene exactly onto the panel; the "cover" value is
 * 1.18x, the crop the reference transition ends on. */
#define COMP_ZOOM_HOME 256
#define COMP_ZOOM_COVER 302

/* q8 blur, walked by wallpaper_composite() as L0 -> L1 -> L2. */
#define COMP_BLUR_HOME 0
#define COMP_BLUR_COVER 256

/* q8 dim toward the top of the sky, so lock-screen text keeps its contrast. */
#define COMP_SCRIM_HOME 0
#define COMP_SCRIM_LOCK 64
#define COMP_SCRIM_COVER 0

/* One transition. Measured off ref.mp4: the reference blur settles in about
 * 233 ms (out/vid/band.raw frames 193..207 at 60 fps). 320 ms is the shell's
 * own number -- long enough that the ease-out tail is visible as motion
 * rather than a cut, short enough that a tap never waits on it. */
#define COMP_DUR_MS 320

void compositor_init(void);

/* Which backdrop the window being opened wants. Called from wm_open(). A
 * window that declares itself `opaque` (the boot-mode screens) takes the
 * wallpaper away outright, so the gate can prove that an opaque window never
 * receives wallpaper ink. */
void compositor_to(int app_id);

/* Snaps the three tweens to wherever compositor_to() last pointed them, with
 * no ramp. The boot path uses it: the first screen has no previous state to
 * transition out of. */
void compositor_settle(void);

/* Advances the tweens to now_ms, paints the backdrop when one is live, then
 * lets the window manager draw and presents. Returns 1 when the backdrop was
 * painted. */
int compositor_frame(uint64_t now_ms);

/* 1 while any tween is still moving: the caller may then ask for the next
 * frame immediately instead of idling. */
int compositor_needs_frame(void);

int compositor_backdrop_live(void);
int compositor_zoom(void);
int compositor_blur(void);
int compositor_scrim(void);

/* Frames the backdrop has been painted since boot - the gate's count. */
uint32_t compositor_backdrop_frames(void);

#endif
