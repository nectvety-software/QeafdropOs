/* SPDX-License-Identifier: MIT */
/* bsp/violet/backlight.c - DSI panel backlight stub */

#include "backlight.h"

long violet_bl_level(int pct, long max) {
    return (long)pct * max / 100;
}

int violet_bl_scan(const char *root, void *out) {
    (void)root; (void)out;
    return 0;
}

const char *violet_bl_reason(void) {
    return "backlight stub";
}

int violet_bl_open(const char *dev, char *dir, int cap, long *max) {
    (void)dev; (void)dir; (void)cap; (void)max;
    return -1;
}

long violet_bl_actual(const char *dir) {
    (void)dir;
    return 0;
}

int violet_bl_write(const char *dir, long level, char *out, int cap) {
    (void)dir; (void)level; (void)out; (void)cap;
    return -1;
}

int violet_bl_apply(const char *dir, long want, char *out, int cap) {
    (void)dir; (void)want; (void)out; (void)cap;
    return -1;
}
