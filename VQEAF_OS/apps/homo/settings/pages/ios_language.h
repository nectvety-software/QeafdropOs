#ifndef IOS_LANGUAGE_H
#define IOS_LANGUAGE_H

#include "lvgl.h"

void ios_language_create(lv_obj_t * parent, const lv_font_t * font,
                         const lv_font_t * small_font,
                         const lv_font_t * heading_font);
void ios_language_refresh(void);

#endif
