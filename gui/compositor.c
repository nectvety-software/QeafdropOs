/* SPDX-License-Identifier: MIT */
/* gui/compositor.c - backdrop state machine + per-frame present. */

#include "compositor.h"
#include "gfx/anim.h"
#include "gfx/wallpaper.h"
#include "gfx/gfx.h"
#include "window_manager.h"

static struct anim g_zoom;
static struct anim g_blur;
static struct anim g_scrim;

/* 1 while the front window lets the backdrop through. Windows that cover the
 * whole panel (recovery, fastboot) set `opaque` and the wallpaper stops being
 * drawn at all - not painted under them, which would be pure waste. */
static int g_live;
static int g_moving;
static uint32_t g_backdrop_frames;

static int target_zoom(int app_id)
{
    return app_id == UMOS_APP_HOME ? COMP_ZOOM_HOME : COMP_ZOOM_COVER;
}

static int target_scrim(int app_id)
{
    if (app_id == UMOS_APP_HOME) return COMP_SCRIM_HOME;
    return app_id == UMOS_APP_LOCK ? COMP_SCRIM_LOCK : COMP_SCRIM_COVER;
}

void compositor_init(void)
{
    g_zoom.value_q = g_zoom.to_q = g_zoom.from_q = COMP_ZOOM_HOME;
    g_blur.value_q = g_blur.to_q = g_blur.from_q = COMP_BLUR_HOME;
    g_scrim.value_q = g_scrim.to_q = g_scrim.from_q = COMP_SCRIM_HOME;
    g_zoom.started = g_blur.started = g_scrim.started = 1;
    g_zoom.dur_ms = g_blur.dur_ms = g_scrim.dur_ms = COMP_DUR_MS;
    g_live = 0;
    g_moving = 0;
    g_backdrop_frames = 0;
}

void compositor_to(int app_id)
{
    struct wm_window *w = wm_find(app_id);

    if (!w || w->opaque) {
        g_live = 0;
        g_moving = 0;
        return;
    }

    g_live = 1;
    anim_set(&g_zoom, target_zoom(app_id), COMP_DUR_MS);
    /* Sharp only ever goes with the unzoomed home view; anything else sits at
     * the cover crop and is blurred behind it. */
    anim_set(&g_blur, app_id == UMOS_APP_HOME ? COMP_BLUR_HOME : COMP_BLUR_COVER,
             COMP_DUR_MS);
    anim_set(&g_scrim, target_scrim(app_id), COMP_DUR_MS);
    g_moving = 1;
}

/* Boots straight into a screen: the shell has no previous state to leave, so
 * the first frame must already be the settled one. Without this the panel
 * would ramp out of a sharp home view nobody was shown. */
void compositor_settle(void)
{
    anim_finish(&g_zoom);
    anim_finish(&g_blur);
    anim_finish(&g_scrim);
    g_moving = 0;
}

int compositor_frame(uint64_t now_ms)
{
    int moved = 0;

    moved |= anim_step(&g_zoom, now_ms);
    moved |= anim_step(&g_blur, now_ms);
    moved |= anim_step(&g_scrim, now_ms);
    g_moving = moved;

    if (g_live) {
        wallpaper_composite(g_zoom.value_q, g_blur.value_q, g_scrim.value_q);
        g_backdrop_frames++;
    }

    wm_frame();
    gfx_flush(0, 0, (int)gfx_width(), (int)gfx_height());

    return g_live;
}

int compositor_needs_frame(void) { return g_moving; }
int compositor_backdrop_live(void) { return g_live; }
int compositor_zoom(void) { return g_zoom.value_q; }
int compositor_blur(void) { return g_blur.value_q; }
int compositor_scrim(void) { return g_scrim.value_q; }
uint32_t compositor_backdrop_frames(void) { return g_backdrop_frames; }
