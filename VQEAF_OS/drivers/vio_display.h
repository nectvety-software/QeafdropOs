/* SPDX-License-Identifier: MIT */
/* platform/violet/vio_display.h - glue between the LVGL display and the
 * display_* rotation seam that fs/rescue.c uses. */
#ifndef HOMO_VIO_DISPLAY_H
#define HOMO_VIO_DISPLAY_H

#include "panel_scale.h"

#include "hal/include/display.h"

void vio_display_bind(vio_panel_t *panel);

/* The canvas the desktop wants for an orientation: 540x1170 portrait (an
 * exact 2x fit of the 1080x2340 panel) or 800x480 landscape. App modules are
 * not covered here; the caller asks for their canvas explicitly. */
void vio_ui_canvas_size(enum display_orientation o, int *w, int *h);

/* Returns 1 once after a rotation change, so the next flush repaints the
 * whole panel (the old orientation's pixels are still on screen). */
int vio_display_take_redraw(void);

#endif /* HOMO_VIO_DISPLAY_H */
