/* SPDX-License-Identifier: MIT */
/* gui/window_manager.c - Simple window manager */

#include "window_manager.h"
#include "compositor.h"
#include "gfx.h"
#include "theme.h"
#include <string.h>

static struct wm_window *g_windows[UMOS_MAX_WINDOWS];
static int g_window_count = 0;
static int g_current = -1;
static int g_sp = 0;

/* Split state. Pane rects are LOGICAL pixels, so rotation is handled by the
 * gfx clip transform on the way to the framebuffer. */
static int g_split_on;
static int g_split_left, g_split_right;
static int g_split_ratio = 128;
static int g_split_lx, g_split_ly, g_split_lw, g_split_lh;
static int g_split_rx, g_split_ry, g_split_rw, g_split_rh;

struct float_win {
    int app_id;
    int x, y, w, h;
};
static struct float_win g_floats[WM_FLOAT_MAX];
static int g_nfloats;

void wm_init(void) {
    g_window_count = 0;
    g_current = -1;
    g_sp = 0;
    g_split_on = 0;
    g_split_left = g_split_right = -1;
    g_split_ratio = 128;
    g_nfloats = 0;
    gfx_clip_reset();
}

void wm_register_window(int app_id, struct wm_window *w) {
    if (g_window_count >= UMOS_MAX_WINDOWS) return;
    w->id = app_id;
    g_windows[g_window_count++] = w;
}

struct wm_window *wm_find(int app_id) {
    for (int i = 0; i < g_window_count; i++) {
        if (g_windows[i] && g_windows[i]->id == app_id) return g_windows[i];
    }
    return NULL;
}

void wm_open(int app_id) {
    struct wm_window *w = wm_find(app_id);
    if (!w) return;
    g_current = app_id;
    for (int i = 0; i < g_window_count; i++) {
        if (g_windows[i]) g_windows[i]->visible = (g_windows[i]->id == app_id);
    }
    /* Arms the backdrop transition. The tween takes its start time on the
     * first compositor_frame(), so switching screens needs no clock here. */
    compositor_to(app_id);
    if (w->visible) w->draw(w);
}

void wm_close(int app_id) {
    if (g_current == app_id) g_current = -1;
    struct wm_window *w = wm_find(app_id);
    if (w) w->visible = 0;
}

int wm_current(void) { return g_current; }

const char *wm_current_name(void) {
    struct wm_window *w = wm_find(g_current);
    return w ? w->name : "?";
}

void wm_dispatch(void) {
    /* Poll input events and route to current window */
    (void)0;
}

void wm_frame(void) {
    if (g_current < 0) return;
    struct wm_window *w = wm_find(g_current);
    if (w && w->visible) w->draw(w);
}

int wm_app_id_from_name(const char *name) {
    if (strcmp(name, "home") == 0) return UMOS_APP_HOME;
    if (strcmp(name, "lock") == 0) return UMOS_APP_LOCK;
    if (strcmp(name, "settings") == 0) return UMOS_APP_SETTINGS;
    if (strcmp(name, "messages") == 0) return UMOS_APP_MESSAGES;
    if (strcmp(name, "gallery") == 0) return UMOS_APP_GALLERY;
    if (strcmp(name, "browser") == 0) return UMOS_APP_BROWSER;
    if (strcmp(name, "recovery") == 0) return UMOS_APP_RECOVERY;
    if (strcmp(name, "fastboot") == 0) return UMOS_APP_FASTBOOT;
    return -1;
}

/* ---- split screen ---------------------------------------------------------- */

static void split_compute(void) {
    int w = (int)gfx_logical_width();
    int h = (int)gfx_logical_height();
    int lw = w * g_split_ratio / 256;
    if (lw < 64) lw = 64;
    if (lw > w - 64) lw = w - 64;
    g_split_lx = 0; g_split_ly = 0; g_split_lw = lw; g_split_lh = h;
    g_split_rx = lw; g_split_ry = 0; g_split_rw = w - lw; g_split_rh = h;
}

int wm_set_split(int left_app, int right_app, int ratio_q8) {
    if (!wm_find(left_app) || !wm_find(right_app)) return -1;
    if (left_app == right_app) return -1;
    if (ratio_q8 < 64 || ratio_q8 > 192) return -1;   /* 25% .. 75% */
    g_split_left = left_app;
    g_split_right = right_app;
    g_split_ratio = ratio_q8;
    g_split_on = 1;
    split_compute();
    for (int i = 0; i < g_window_count; i++)
        if (g_windows[i]) g_windows[i]->visible =
            (g_windows[i]->id == left_app || g_windows[i]->id == right_app);
    return 0;
}

void wm_split_end(void) {
    g_split_on = 0;
    g_split_left = g_split_right = -1;
    gfx_clip_reset();
}

int wm_split_active(void) { return g_split_on; }

void wm_split_rects(int *lx, int *ly, int *lw, int *lh,
                    int *rx, int *ry, int *rw, int *rh) {
    if (g_split_on) split_compute();
    if (lx) *lx = g_split_lx; if (ly) *ly = g_split_ly;
    if (lw) *lw = g_split_lw; if (lh) *lh = g_split_lh;
    if (rx) *rx = g_split_rx; if (ry) *ry = g_split_ry;
    if (rw) *rw = g_split_rw; if (rh) *rh = g_split_rh;
}

int wm_split_apps(int *left, int *right) {
    if (!g_split_on) return -1;
    if (left) *left = g_split_left;
    if (right) *right = g_split_right;
    return 0;
}

int wm_split_hit(int lx, int ly) {
    if (!g_split_on) return -1;
    split_compute();
    if (lx >= g_split_lx && lx < g_split_lx + g_split_lw &&
        ly >= g_split_ly && ly < g_split_ly + g_split_lh) return 0;
    if (lx >= g_split_rx && lx < g_split_rx + g_split_rw &&
        ly >= g_split_ry && ly < g_split_ry + g_split_rh) return 1;
    return -1;
}

/* ---- floating windows ------------------------------------------------------ */

static int float_index(int app_id) {
    for (int i = 0; i < g_nfloats; i++) if (g_floats[i].app_id == app_id) return i;
    return -1;
}

int wm_float_add(int app_id, int x, int y, int w, int h) {
    struct float_win f;
    if (!wm_find(app_id) || g_nfloats >= WM_FLOAT_MAX) return -1;
    if (w < 64 || h < 64) return -1;
    if (float_index(app_id) >= 0) return -1;
    f.app_id = app_id; f.x = x; f.y = y; f.w = w; f.h = h;
    g_floats[g_nfloats++] = f;      /* newest is on top */
    return 0;
}

int wm_float_remove(int app_id) {
    int i = float_index(app_id);
    if (i < 0) return -1;
    memmove(&g_floats[i], &g_floats[i + 1],
            (size_t)(g_nfloats - i - 1) * sizeof(g_floats[0]));
    g_nfloats--;
    return 0;
}

int wm_float_raise(int app_id) {
    int i = float_index(app_id);
    struct float_win f;
    if (i < 0 || i == g_nfloats - 1) return i < 0 ? -1 : 0;
    f = g_floats[i];
    memmove(&g_floats[i], &g_floats[i + 1],
            (size_t)(g_nfloats - i - 1) * sizeof(g_floats[0]));
    g_floats[g_nfloats - 1] = f;
    return 0;
}

int wm_float_move(int app_id, int x, int y) {
    int i = float_index(app_id);
    int w = (int)gfx_logical_width(), h = (int)gfx_logical_height();
    if (i < 0) return -1;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x + g_floats[i].w > w) x = w - g_floats[i].w;
    if (y + g_floats[i].h > h) y = h - g_floats[i].h;
    g_floats[i].x = x;
    g_floats[i].y = y;
    return 0;
}

int wm_float_count(void) { return g_nfloats; }

int wm_float_top_at(int lx, int ly) {
    for (int i = g_nfloats - 1; i >= 0; i--)
        if (lx >= g_floats[i].x && lx < g_floats[i].x + g_floats[i].w &&
            ly >= g_floats[i].y && ly < g_floats[i].y + g_floats[i].h)
            return g_floats[i].app_id;
    return -1;
}

void wm_float_rect(int index, int *app_id, int *x, int *y, int *w, int *h) {
    if (index < 0 || index >= g_nfloats) {
        if (app_id) *app_id = -1;
        if (x) *x = 0; if (y) *y = 0; if (w) *w = 0; if (h) *h = 0;
        return;
    }
    if (app_id) *app_id = g_floats[index].app_id;
    if (x) *x = g_floats[index].x; if (y) *y = g_floats[index].y;
    if (w) *w = g_floats[index].w; if (h) *h = g_floats[index].h;
}

/* Draws each pane/window with the gfx clip set to its own rectangle. This is
 * what makes a full-screen bootui draw() safe to reuse inside a pane. */
int wm_frame_multitask(void) {
    int drawn = 0;
    if (!g_split_on && !g_nfloats) return 0;
    if (g_split_on) {
        struct wm_window *a = wm_find(g_split_left);
        struct wm_window *b = wm_find(g_split_right);
        split_compute();
        if (a) {
            gfx_set_clip(g_split_lx, g_split_ly, g_split_lw, g_split_lh);
            a->draw(a); drawn++;
        }
        if (b) {
            gfx_set_clip(g_split_rx, g_split_ry, g_split_rw, g_split_rh);
            b->draw(b); drawn++;
        }
    }
    for (int i = 0; i < g_nfloats; i++) {
        struct wm_window *w = wm_find(g_floats[i].app_id);
        if (!w) continue;
        gfx_set_clip(g_floats[i].x, g_floats[i].y,
                     g_floats[i].w, g_floats[i].h);
        w->draw(w);
        drawn++;
    }
    gfx_clip_reset();
    return drawn;
}
