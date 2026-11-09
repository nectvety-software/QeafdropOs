#ifndef HACHIMI_IOS_STATUS_BAR_H
#define HACHIMI_IOS_STATUS_BAR_H

#include "lvgl.h"

/* Creates the status strip and returns its container.  The container is what
 * the shell moves/resizes when the display switches between portrait and
 * landscape; the clock and the two icons are laid out relative to its width. */
lv_obj_t * ios_status_bar_create(lv_obj_t * parent, lv_color_t foreground);
/* Sizes the strip and its contents from the iOS proportions on this canvas:
 * 32 px tall on the 800x480 app canvas, 46 px on the 540x1170 portrait shell. */
void ios_status_bar_set_size(lv_obj_t * bar, lv_coord_t width, lv_coord_t height);

/* Legacy helper: a lone clock label on `parent` at the status-bar origin. */
void ios_status_bar_create_time_label(lv_obj_t * parent, lv_color_t foreground);

#endif
