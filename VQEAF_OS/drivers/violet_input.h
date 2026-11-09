/* SPDX-License-Identifier: MIT */
/* platform/violet/violet_input.h - evdev multitouch for the violet panel. */
#ifndef HOMO_VIOLET_INPUT_H
#define HOMO_VIOLET_INPUT_H

#include "panel_scale.h"

struct vio_touch {
    int fd;
    int use_mt;
    int min_x, max_x, min_y, max_y;
    int slot;
    int pending_x, pending_y;
    int have_x, have_y;
    int pending_pressed;
    int pressed;
    int x, y;
    vio_panel_t *panel;
};

int vio_touch_open(struct vio_touch *touch, const char *path, vio_panel_t *panel);
void vio_touch_close(struct vio_touch *touch);
void vio_touch_poll(struct vio_touch *touch);

#endif /* HOMO_VIOLET_INPUT_H */
