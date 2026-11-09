/* SPDX-License-Identifier: MIT */
/* Lock app - QeafdropOs .qeapp (Homo LVGL) */
#include "lvgl.h"

static lv_obj_t * create(lv_obj_t * parent)
{
    lv_obj_t * label = lv_label_create(parent);
    lv_label_set_text(label, "Lock");
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    return parent;
}

const void * q_create = create;
