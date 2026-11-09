/* SPDX-License-Identifier: MIT */
/* gui/wm_nav.c - App navigation stack */

#include "wm_nav.h"
#include "gui/window_manager.h"
#include <string.h>

static int g_stack[WM_MAX_DEPTH];
static int g_depth = 0;

int wm_nav_push(int app_id) {
    if (g_depth >= WM_MAX_DEPTH) return -1;
    g_stack[g_depth++] = app_id;
    wm_open(app_id);
    return g_depth - 1;
}

int wm_nav_pop(void) {
    if (g_depth <= 0) return -1;
    g_depth--;
    int prev = g_depth > 0 ? g_stack[g_depth - 1] : -1;
    wm_open(prev);
    return prev;
}

int wm_nav_replace(int app_id) {
    if (g_depth <= 0) return -1;
    g_stack[g_depth - 1] = app_id;
    wm_open(app_id);
    return g_depth - 1;
}

int wm_nav_clear(void) {
    g_depth = 0;
    wm_open(-1);
    return 0;
}

int wm_nav_depth(void) { return g_depth; }

int wm_nav_top(void) {
    return g_depth > 0 ? g_stack[g_depth - 1] : -1;
}

void wm_nav_go_home(void) {
    wm_nav_clear();
    wm_nav_push(0); /* UMOS_APP_HOME */
}

void wm_nav_go_lock(void) {
    wm_nav_clear();
    wm_nav_push(1); /* UMOS_APP_LOCK */
}
