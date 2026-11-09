/* SPDX-License-Identifier: MIT */
/* tests/display_default_test.c - locks the shipped orientation default.
 *
 * The phone boots portrait unless a stored prefs record says otherwise, so a
 * silent revert to landscape only shows up on the glass. This gate reads the
 * real display_hal.c state; the setter round-trip is the negative control that
 * proves the getter is not echoing a constant.
 */
#include "vio_display.h"

#include "hal/include/display.h"
#include "violet.h"

#include <stdio.h>
#include <string.h>

void vio_log(const char *fmt, ...)
{
    (void)fmt;
}

static int failures;
static int last_rotation = -1;

/* panel_scale.c owns this call; the test only records what was asked. */
void vio_panel_set_rotation(vio_panel_t *panel, int rotation)
{
    (void)panel;
    last_rotation = rotation;
}

static void check(int ok, const char *what)
{
    if(ok) {
        printf("ok    %s\n", what);
        return;
    }
    printf("FAIL  %s\n", what);
    failures++;
}

int main(void)
{
    check(display_orientation() == DISPLAY_PORTRAIT,
          "the shipped default orientation is portrait");
    check(display_orientation_degrees(display_orientation()) == 0,
          "portrait means zero degrees of panel rotation");
    check(display_parse_orientation("0") == DISPLAY_PORTRAIT,
          "rotate 0 parses to the shipped default");
    check(display_auto_rotate() == 0,
          "auto-rotate starts off, so a sensor cannot flip the default");

    /* Negative control: the getter follows the setter, and binding the panel
     * forwards the current orientation instead of a remembered one. */
    display_set_orientation(DISPLAY_LANDSCAPE);
    check(display_orientation() == DISPLAY_LANDSCAPE,
          "the getter reports a landscape set, so it is not a constant");
    display_set_orientation(DISPLAY_PORTRAIT);
    check(display_orientation() == DISPLAY_PORTRAIT,
          "returning to portrait is reported back");

    {
        vio_panel_t panel;
        memset(&panel, 0, sizeof(panel));
        vio_display_bind(&panel);
        check(last_rotation == 0,
              "binding the panel applies zero degrees in portrait");
    }

    /* The canvas the shell asks LVGL for, per orientation. Portrait must be the
     * exact 2x fit of the panel or the home screen is a strip with black bars
     * (that was measured on the glass); landscape keeps the drawn 800x480. */
    {
        int w = 0, h = 0;

        vio_ui_canvas_size(DISPLAY_PORTRAIT, &w, &h);
        check(w == VIO_UI_PORTRAIT_W && h == VIO_UI_PORTRAIT_H,
              "portrait asks for the exact-fit 540x1170 canvas");
        check(w * 2 == VIO_PANEL_W && h * 2 == VIO_PANEL_H,
              "the portrait canvas is exactly half the panel in both axes");
        vio_ui_canvas_size(DISPLAY_PORTRAIT_REV, &w, &h);
        check(w == VIO_UI_PORTRAIT_W && h == VIO_UI_PORTRAIT_H,
              "portrait-rev asks for the same canvas");
        vio_ui_canvas_size(DISPLAY_LANDSCAPE, &w, &h);
        check(w == VIO_UI_W && h == VIO_UI_H,
              "landscape keeps the 800x480 canvas");
        vio_ui_canvas_size(DISPLAY_LANDSCAPE_REV, &w, &h);
        check(w == VIO_UI_W && h == VIO_UI_H,
              "landscape-rev keeps the 800x480 canvas");
    }

    printf("\n12 checks, %d failures\n", failures);
    return failures ? 1 : 0;
}
