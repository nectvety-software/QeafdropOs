/* SPDX-License-Identifier: MIT */
/* bsp/violet/backlight.h - DSI panel backlight */

#ifndef VIOLET_BACKLIGHT_H
#define VIOLET_BACKLIGHT_H

long violet_bl_level(int pct, long max);
int violet_bl_scan(const char *root, void *out);
const char *violet_bl_reason(void);
int violet_bl_open(const char *dev, char *dir, int cap, long *max);
long violet_bl_actual(const char *dir);
int violet_bl_write(const char *dir, long level, char *out, int cap);
int violet_bl_apply(const char *dir, long want, char *out, int cap);

#endif
