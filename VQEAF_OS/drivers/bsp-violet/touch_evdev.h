/* SPDX-License-Identifier: MIT */
/* bsp/violet/touch_evdev.h - Touch input via evdev */

#ifndef VIOLET_TOUCH_EVDEV_H
#define VIOLET_TOUCH_EVDEV_H

int violet_touch_init(void);
int violet_touch_poll(int *x, int *y, int *pressed);

#endif
