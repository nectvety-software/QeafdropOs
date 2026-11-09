/* SPDX-License-Identifier: MIT */
/* platform/violet/display_hal.c - the display seam from hal/include/display.h.
 *
 * fs/rescue.c's `rotate` verb talks to this file. It owns exactly one piece of
 * state: which of the four portrait/landscape orientations the UI is rendered
 * in. The scaler does the actual transform; changing rotation only asks for a
 * full repaint.
 */
#include "vio_display.h"

#include "violet.h"

#include "hal/include/display.h"

#include <string.h>

static vio_panel_t *g_panel;
static enum display_orientation g_orientation = DISPLAY_PORTRAIT;
static int g_auto_rotate;
static int g_sensor_x, g_sensor_y;
static volatile int g_full_redraw = 1;

static int abs_int(int value)
{
    return value < 0 ? -value : value;
}

void vio_display_bind(vio_panel_t *panel)
{
    g_panel = panel;
    vio_panel_set_rotation(panel, display_orientation_degrees(g_orientation));
    g_full_redraw = 1;
}

int vio_display_take_redraw(void)
{
    int redraw = g_full_redraw;
    g_full_redraw = 0;
    return redraw;
}

void vio_ui_canvas_size(enum display_orientation o, int *w, int *h)
{
    if(o == DISPLAY_LANDSCAPE || o == DISPLAY_LANDSCAPE_REV) {
        *w = VIO_UI_LAND_W;
        *h = VIO_UI_LAND_H;
    }
    else {
        *w = VIO_UI_PORTRAIT_W;
        *h = VIO_UI_PORTRAIT_H;
    }
}

const char *display_orientation_name(enum display_orientation o)
{
    switch(o) {
    case DISPLAY_PORTRAIT:      return "portrait";
    case DISPLAY_LANDSCAPE:     return "landscape";
    case DISPLAY_PORTRAIT_REV:  return "portrait-rev";
    case DISPLAY_LANDSCAPE_REV: return "landscape-rev";
    default:                    return "unknown";
    }
}

int display_orientation_degrees(enum display_orientation o)
{
    switch(o) {
    case DISPLAY_LANDSCAPE:     return 90;
    case DISPLAY_PORTRAIT_REV:  return 180;
    case DISPLAY_LANDSCAPE_REV: return 270;
    default:                    return 0;
    }
}

enum display_orientation display_set_orientation(enum display_orientation o)
{
    if(o < 0 || o >= DISPLAY_ORIENTATION_COUNT) return g_orientation;
    if(o != g_orientation) {
        g_orientation = o;
        if(g_panel) {
            vio_panel_set_rotation(g_panel, display_orientation_degrees(o));
            g_full_redraw = 1;
        }
        vio_log("display: orientation -> %s (%d degrees)",
                display_orientation_name(o), display_orientation_degrees(o));
    }
    return g_orientation;
}

enum display_orientation display_orientation(void)
{
    return g_orientation;
}

void display_set_auto_rotate(int on)
{
    g_auto_rotate = on ? 1 : 0;
}

int display_auto_rotate(void)
{
    return g_auto_rotate;
}

void display_feed_sensor(int gx, int gy)
{
    g_sensor_x = gx;
    g_sensor_y = gy;
}

void display_poll(void)
{
    int ax, ay;

    if(!g_auto_rotate) return;
    ax = abs_int(g_sensor_x);
    ay = abs_int(g_sensor_y);
    if(ax < 350 && ay < 350) return; /* lying flat: hold the current view */
    if(ax * 10 > ay * 13) {
        display_set_orientation(g_sensor_x > 0 ? DISPLAY_LANDSCAPE_REV
                                               : DISPLAY_LANDSCAPE);
    }
    else if(ay * 10 > ax * 13) {
        display_set_orientation(g_sensor_y > 0 ? DISPLAY_PORTRAIT_REV
                                               : DISPLAY_PORTRAIT);
    }
}

int display_parse_orientation(const char *text)
{
    if(text == NULL) return -1;
    if(strcmp(text, "0") == 0) return DISPLAY_PORTRAIT;
    if(strcmp(text, "90") == 0) return DISPLAY_LANDSCAPE;
    if(strcmp(text, "180") == 0) return DISPLAY_PORTRAIT_REV;
    if(strcmp(text, "270") == 0) return DISPLAY_LANDSCAPE_REV;
    if(strcmp(text, "auto") == 0) return -2;
    return -1;
}
