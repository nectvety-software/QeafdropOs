#ifndef HACHIMI_IOS_DESKTOP_H
#define HACHIMI_IOS_DESKTOP_H
#include "app_registry.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Default-product compatibility wrapper; implemented in product/default_apps.c. */
void ios_desktop_create(void);
bool hachimi_desktop_create(const hachimi_app_catalog_t * catalog);
bool hachimi_desktop_launch(hachimi_app_id_t id);
bool hachimi_desktop_home(void);
void hachimi_desktop_destroy(void);
void hachimi_desktop_set_app_gestures_enabled(bool enabled);

/* The platform resizes the LVGL display to the canvas the shell or the app
 * module needs, then calls this to re-measure and restart a waiting launch. */
void hachimi_desktop_relayout(void);
/* True from the moment a launch is waiting for the 800x480 app canvas until
 * the app has closed, so the platform knows when to switch canvases. */
bool hachimi_desktop_app_mode(void);
/* True when the pending/open app is Settings, which runs on the shell canvas
 * (540x1170 portrait / 1170x540 landscape) instead of 800x480. */
bool hachimi_desktop_app_is_settings(void);

/* The Control Center blurs a snapshot of Home, but iOS never blurs the status
 * bar: it stays crisp above the sheet. Hiding it for the capture keeps the
 * blurred wallpaper free of a second, smeared clock behind the sheet's own
 * status row. */
void hachimi_desktop_set_status_bar_visible(bool visible);

typedef struct {
    void * user_data;
    /* Borrowed RGB565 buffers remain valid until end(). content is optional;
     * NULL requests the lightweight launch panel / snapshot-failure fallback. */
    bool (*begin)(void * user_data, const lv_draw_buf_t * background,
                  const lv_draw_buf_t * content,
                  int32_t x, int32_t y, int32_t width, int32_t height, int32_t radius);
    void (*draw)(void * user_data, int32_t x, int32_t y, int32_t width, int32_t height,
                 int32_t radius, bool reveal_target);
    void (*end)(void * user_data);
    void (*present)(void * user_data);
} hachimi_desktop_transition_backend_t;

void hachimi_desktop_set_transition_backend(const hachimi_desktop_transition_backend_t * backend);

#ifdef __cplusplus
}
#endif

#endif
