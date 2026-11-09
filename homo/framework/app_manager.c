#include "app_manager.h"
#include <string.h>

void hachimi_app_manager_init(hachimi_app_manager_t * manager, lv_obj_t * parent)
{
    memset(manager, 0, sizeof(*manager));
    manager->parent = parent;
}

void hachimi_app_manager_close(hachimi_app_manager_t * manager)
{
    if(manager->active == NULL) return;
    hachimi_app_instance_t * instance = manager->active;
    if(instance->module->on_close) instance->module->on_close(instance->view);
    lv_obj_add_flag(instance->view, LV_OBJ_FLAG_HIDDEN);
    manager->active = NULL;
}

bool hachimi_app_manager_open(hachimi_app_manager_t * manager, const hachimi_app_module_t * module)
{
    if(module == NULL) {
        hachimi_app_manager_close(manager);
        return true;
    }
    hachimi_app_instance_t * instance = NULL;
    for(size_t i = 0; i < manager->count; i++) {
        if(manager->instances[i].module == module) instance = &manager->instances[i];
    }
    if(instance == NULL) {
        if(module->create == NULL || manager->count >= HACHIMI_MAX_APP_INSTANCES) return false;
        lv_obj_t * view = module->create(manager->parent);
        if(view == NULL) return false;
        instance = &manager->instances[manager->count++];
        instance->module = module;
        instance->view = view;
    }
    hachimi_app_manager_close(manager);
    manager->active = instance;
    lv_obj_clear_flag(instance->view, LV_OBJ_FLAG_HIDDEN);
    if(module->on_open) module->on_open(instance->view);
    return true;
}

void hachimi_app_manager_layout(hachimi_app_manager_t * manager, lv_coord_t width, lv_coord_t height)
{
    if(manager->active == NULL) return;
    lv_obj_set_pos(manager->active->view, 0, 0);
    lv_obj_set_size(manager->active->view, width, height);
}

void hachimi_app_manager_set_opacity(hachimi_app_manager_t * manager, lv_opa_t opacity)
{
    if(manager->active) lv_obj_set_style_opa(manager->active->view, opacity, LV_PART_MAIN);
}

size_t hachimi_app_manager_count(const hachimi_app_manager_t * manager)
{
    return manager ? manager->count : 0;
}

const hachimi_app_module_t * hachimi_app_manager_module_at(const hachimi_app_manager_t * manager, size_t index)
{
    if(manager == NULL || index >= manager->count) return NULL;
    return manager->instances[index].module;
}

bool hachimi_app_manager_evict(hachimi_app_manager_t * manager, const hachimi_app_module_t * module)
{
    if(manager == NULL || module == NULL) return false;
    for(size_t i = 0; i < manager->count; i++) {
        if(manager->instances[i].module != module) continue;
        if(manager->active == &manager->instances[i]) {
            if(module->on_close) module->on_close(manager->instances[i].view);
            manager->active = NULL;
        }
        lv_obj_delete(manager->instances[i].view);
        for(size_t j = i + 1; j < manager->count; j++)
            manager->instances[j - 1] = manager->instances[j];
        manager->count--;
        /* `active` may have pointed into the shifted tail; recompute by module. */
        if(manager->active != NULL) {
            bool found = false;
            for(size_t k = 0; k < manager->count; k++)
                if(&manager->instances[k] == manager->active) found = true;
            if(!found) manager->active = NULL;
        }
        return true;
    }
    return false;
}

void hachimi_app_manager_destroy(hachimi_app_manager_t * manager)
{
    hachimi_app_manager_close(manager);
    for(size_t i = 0; i < manager->count; i++) lv_obj_delete(manager->instances[i].view);
    lv_obj_t * parent = manager->parent;
    hachimi_app_manager_init(manager, parent);
}
