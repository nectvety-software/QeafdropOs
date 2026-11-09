/* SPDX-License-Identifier: MIT */
/* bsp/violet/touch_evdev.c - Touch input stub */

#include "touch_evdev.h"

int violet_touch_init(void) {
    return 0;
}

int violet_touch_poll(int *x, int *y, int *pressed) {
    (void)x; (void)y; (void)pressed;
    return 0;
}
