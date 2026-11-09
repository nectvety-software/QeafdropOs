/* SPDX-License-Identifier: MIT */
/* hal/include/display.h - screen rotation seam.
 *
 * The panel is physically portrait (1080x2340). `display_set_orientation()`
 * only changes how logical drawing maps onto it (gui/gfx/gfx.c does the
 * transform). Auto-rotate feeds a gravity vector through
 * display_feed_sensor(); the board / a host gate supplies that vector, so the
 * policy below is measured without needing a real accelerometer.
 *
 * Rotation never writes a partition and never reboots. It is a display
 * property, published in DEVINFO.TXT so VQEAF Tool can read it.
 */

#ifndef UMOS_DISPLAY_H
#define UMOS_DISPLAY_H

#include <stdint.h>

enum display_orientation {
    DISPLAY_PORTRAIT = 0,       /*   0 degrees */
    DISPLAY_LANDSCAPE,          /*  90 degrees */
    DISPLAY_PORTRAIT_REV,       /* 180 degrees */
    DISPLAY_LANDSCAPE_REV,      /* 270 degrees */
    DISPLAY_ORIENTATION_COUNT
};

/* Returns the orientation actually applied. Invalid values are refused. */
enum display_orientation display_set_orientation(enum display_orientation o);
enum display_orientation display_orientation(void);
const char *display_orientation_name(enum display_orientation o);
int display_orientation_degrees(enum display_orientation o);

void display_set_auto_rotate(int on);
int  display_auto_rotate(void);

/* Gravity in milli-g, device frame. A host gate feeds synthetic values; the
 * board would read /sys/bus/iio/devices/iio:device0/in_accel_*_raw. */
void display_feed_sensor(int gx, int gy);
/* Applies the sensor vector when auto-rotate is on. Called once per tick. */
void display_poll(void);

/* Parse "0"/"90"/"180"/"270"/"auto" as used by the rescue `rotate` verb.
 * Returns -1 for anything else. -2 means "turn auto-rotate on". */
int display_parse_orientation(const char *text);

#endif
