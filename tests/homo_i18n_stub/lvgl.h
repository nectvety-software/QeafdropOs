#ifndef HOMO_I18N_STUB_LVGL_H
#define HOMO_I18N_STUB_LVGL_H

typedef const char * (*lv_label_translate_cb_t)(const char * text);
void lv_label_set_translate_cb(lv_label_translate_cb_t cb);

#endif
