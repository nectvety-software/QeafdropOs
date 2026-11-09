#ifndef HACHIMI_CONTROL_CENTER_CONTROLS_H
#define HACHIMI_CONTROL_CENTER_CONTROLS_H
#include "lvgl.h"
void hachimi_control_center_controls_create(lv_obj_t * parent);
/* Centre the fixed 800x480 control composition inside the current display.
 * In portrait it sits as a centred card; in landscape it is shifted right. */
void hachimi_control_center_controls_layout(int display_w, int display_h);
void hachimi_control_center_controls_open(void);
void hachimi_control_center_controls_close(void);
void hachimi_control_center_controls_destroy(void);
#endif
