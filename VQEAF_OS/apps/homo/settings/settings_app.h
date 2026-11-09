#ifndef HACHIMI_SETTINGS_APP_H
#define HACHIMI_SETTINGS_APP_H
#include "framework/app.h"
#include <stdbool.h>
extern const hachimi_app_module_t hachimi_settings_app;
/* Vuốt trái/back: nếu đang ở trang chi tiết, về danh sách và trả true. */
bool hachimi_settings_handle_back(void);
#endif
