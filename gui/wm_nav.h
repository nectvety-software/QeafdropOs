/* SPDX-License-Identifier: MIT */
/* gui/wm_nav.h - App navigation stack */

#ifndef UMOS_WM_NAV_H
#define UMOS_WM_NAV_H

#include "gui/window_manager.h"

#define WM_MAX_DEPTH 8

enum wm_nav_action {
    WM_NAV_PUSH,
    WM_NAV_POP,
    WM_NAV_REPLACE,
    WM_NAV_CLEAR
};

int wm_nav_push(int app_id);
int wm_nav_pop(void);
int wm_nav_replace(int app_id);
int wm_nav_clear(void);
int wm_nav_depth(void);
int wm_nav_top(void);
void wm_nav_go_home(void);
void wm_nav_go_lock(void);

#endif
