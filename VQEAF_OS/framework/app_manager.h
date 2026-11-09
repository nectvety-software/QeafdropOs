#ifndef HACHIMI_APP_MANAGER_H
#define HACHIMI_APP_MANAGER_H
#include "app.h"
#include <stdbool.h>
#include <stddef.h>

#define HACHIMI_MAX_APP_INSTANCES 16
typedef struct {
    const hachimi_app_module_t * module;
    lv_obj_t * view;
} hachimi_app_instance_t;

typedef struct {
    lv_obj_t * parent;
    hachimi_app_instance_t instances[HACHIMI_MAX_APP_INSTANCES];
    size_t count;
    hachimi_app_instance_t * active;
} hachimi_app_manager_t;

void hachimi_app_manager_init(hachimi_app_manager_t * manager, lv_obj_t * parent);
bool hachimi_app_manager_open(hachimi_app_manager_t * manager, const hachimi_app_module_t * module);
void hachimi_app_manager_close(hachimi_app_manager_t * manager);
void hachimi_app_manager_destroy(hachimi_app_manager_t * manager);
void hachimi_app_manager_layout(hachimi_app_manager_t * manager, lv_coord_t width, lv_coord_t height);
void hachimi_app_manager_set_opacity(hachimi_app_manager_t * manager, lv_opa_t opacity);

/* Multitasking: read the live instance list (MRU order is the caller's job)
 * and evict one module entirely (view deleted, slot freed) so the switcher
 * "Close" button actually releases its memory. */
size_t hachimi_app_manager_count(const hachimi_app_manager_t * manager);
const hachimi_app_module_t * hachimi_app_manager_module_at(const hachimi_app_manager_t * manager, size_t index);
bool hachimi_app_manager_evict(hachimi_app_manager_t * manager, const hachimi_app_module_t * module);
#endif
