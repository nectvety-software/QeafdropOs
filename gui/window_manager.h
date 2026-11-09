/* SPDX-License-Identifier: MIT */
/* gui/window_manager.h - Simple window manager */

#ifndef UMOS_WINDOW_MANAGER_H
#define UMOS_WINDOW_MANAGER_H

#include "gfx.h"

#define UMOS_MAX_WINDOWS 8
#define UMOS_APP_HOME 0
#define UMOS_APP_LOCK 1
#define UMOS_APP_SETTINGS 2
#define UMOS_APP_MESSAGES 3
#define UMOS_APP_GALLERY 4
#define UMOS_APP_BROWSER 5
#define UMOS_APP_RECOVERY 6
#define UMOS_APP_FASTBOOT 7
#define UMOS_APP_COUNT 8

struct wm_window {
    int id;
    const char *name;
    struct umos_rect bounds;
    int visible;
    int focused;
    /* 1 = this screen paints every pixel of the panel itself, so the
     * compositor retires the wallpaper backdrop instead of drawing under it.
     * Boot-mode screens (recovery, fastboot) set it; shell screens do not. */
    int opaque;
    void (*draw)(struct wm_window *w);
    int (*event)(struct wm_window *w, uint32_t type, uint32_t d1, uint32_t d2);
};

void wm_init(void);
void wm_open(int app_id);
void wm_close(int app_id);
/* The window registered for this app id, or NULL. The compositor reads a
 * window's `opaque` flag through here, which is why it is part of the API. */
struct wm_window *wm_find(int app_id);
int wm_current(void);
const char *wm_current_name(void);
void wm_dispatch(void);
void wm_frame(void);
void wm_register_window(int app_id, struct wm_window *w);
int wm_app_id_from_name(const char *name);

/* ---- split screen ----------------------------------------------------------
 * Two apps share the logical surface. Each window is drawn with the gfx clip
 * set to its own pane, so a full-screen bui_begin() cannot repaint the other
 * half. ratio_q8 is the left pane's share (128 = 50/50). */
int wm_set_split(int left_app, int right_app, int ratio_q8);
void wm_split_end(void);
int wm_split_active(void);
void wm_split_rects(int *lx, int *ly, int *lw, int *lh,
                    int *rx, int *ry, int *rw, int *rh);
int wm_split_apps(int *left, int *right);
/* Which pane a logical touch falls in: 0 left, 1 right, -1 outside. */
int wm_split_hit(int lx, int ly);

/* ---- floating windows ------------------------------------------------------
 * Up to four small windows above the current screen, each with a z-order.
 * Raising moves it to the top; dragging changes its bounds. */
#define WM_FLOAT_MAX 4
int  wm_float_add(int app_id, int x, int y, int w, int h);
int  wm_float_remove(int app_id);
int  wm_float_raise(int app_id);
int  wm_float_move(int app_id, int x, int y);
int  wm_float_count(void);
int  wm_float_top_at(int lx, int ly);
void wm_float_rect(int index, int *app_id, int *x, int *y, int *w, int *h);
/* Draws the active split panes and floating windows in z-order. */
int  wm_frame_multitask(void);

#endif
