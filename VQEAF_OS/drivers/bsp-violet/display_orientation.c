/* SPDX-License-Identifier: MIT */
/* bsp/violet/display_orientation.c - rotation policy for violet.
 *
 * One owner for the orientation state. It calls gfx_set_rotation(), so no
 * other module can change the panel transform behind its back. Auto-rotate
 * uses a deadband and a ratio test: a phone lying flat (|gx|,|gy| both small)
 * keeps the current orientation instead of flipping on noise.
 */

#include "hal/include/display.h"
#include "gui/gfx/gfx.h"
#include "core/log.h"

/* Below this absolute gravity (milli-g) the device is flat: hold. */
#define FLAT_MG        250
/* A dominant axis must beat the other by this factor to win. */
#define DOMINANCE_Q8   332      /* 1.30 in q8 */

static enum display_orientation g_orientation = DISPLAY_PORTRAIT;
static int g_auto;
static int g_sensor_x, g_sensor_y;

static int iabs(int v) { return v < 0 ? -v : v; }

static enum gfx_rotation to_gfx(enum display_orientation o) {
    switch (o) {
    case DISPLAY_LANDSCAPE:     return GFX_ROT_90;
    case DISPLAY_PORTRAIT_REV:  return GFX_ROT_180;
    case DISPLAY_LANDSCAPE_REV: return GFX_ROT_270;
    default:                    return GFX_ROT_0;
    }
}

const char *display_orientation_name(enum display_orientation o) {
    switch (o) {
    case DISPLAY_PORTRAIT:      return "portrait";
    case DISPLAY_LANDSCAPE:     return "landscape";
    case DISPLAY_PORTRAIT_REV:  return "portrait-rev";
    case DISPLAY_LANDSCAPE_REV: return "landscape-rev";
    default:                    return "unknown";
    }
}

int display_orientation_degrees(enum display_orientation o) {
    switch (o) {
    case DISPLAY_LANDSCAPE:     return 90;
    case DISPLAY_PORTRAIT_REV:  return 180;
    case DISPLAY_LANDSCAPE_REV: return 270;
    default:                    return 0;
    }
}

enum display_orientation display_set_orientation(enum display_orientation o) {
    if (o < 0 || o >= DISPLAY_ORIENTATION_COUNT) return g_orientation;
    if (o != g_orientation) {
        g_orientation = o;
        gfx_set_rotation(to_gfx(o));
        umos_logf("display: orientation -> %s (%d degrees)\n",
                  display_orientation_name(o), display_orientation_degrees(o));
    }
    return g_orientation;
}

enum display_orientation display_orientation(void) { return g_orientation; }

void display_set_auto_rotate(int on) {
    g_auto = on ? 1 : 0;
    umos_logf("display: auto-rotate %s\n", g_auto ? "on" : "off");
}

int display_auto_rotate(void) { return g_auto; }

void display_feed_sensor(int gx, int gy) {
    g_sensor_x = gx;
    g_sensor_y = gy;
}

void display_poll(void) {
    int ax, ay;
    enum display_orientation next;

    if (!g_auto) return;
    ax = iabs(g_sensor_x);
    ay = iabs(g_sensor_y);
    if (ax + ay < FLAT_MG) return;              /* lying flat: hold */
    if (ay * 256 >= ax * DOMINANCE_Q8) {
        next = g_sensor_y >= 0 ? DISPLAY_PORTRAIT : DISPLAY_PORTRAIT_REV;
    } else if (ax * 256 >= ay * DOMINANCE_Q8) {
        next = g_sensor_x >= 0 ? DISPLAY_LANDSCAPE : DISPLAY_LANDSCAPE_REV;
    } else {
        return;                                  /* diagonal: hold */
    }
    if (next != g_orientation) display_set_orientation(next);
}

int display_parse_orientation(const char *text) {
    if (!text) return -1;
    if (text[0] == 'a' && text[1] == 'u' && text[2] == 't' && text[3] == 'o' &&
        text[4] == '\0') return -2;
    if (text[0] == '0' && text[1] == '\0') return DISPLAY_PORTRAIT;
    if (text[0] == '9' && text[1] == '0' && text[2] == '\0')
        return DISPLAY_LANDSCAPE;
    if (text[0] == '1' && text[1] == '8' && text[2] == '0' && text[3] == '\0')
        return DISPLAY_PORTRAIT_REV;
    if (text[0] == '2' && text[1] == '7' && text[2] == '0' && text[3] == '\0')
        return DISPLAY_LANDSCAPE_REV;
    return -1;
}
