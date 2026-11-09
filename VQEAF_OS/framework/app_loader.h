#ifndef HACHIMI_APP_LOADER_H
#define HACHIMI_APP_LOADER_H

#include "app.h"
#include <stdbool.h>
#include <stddef.h>

/* Load .qeapp shared objects from the QEAFRESCUE Apps store.
 * Layout on the FAT16 volume (see homo/docs/qeapp.md):
 *
 *   /Apps/<id>.qeapp        ZIP (manifest.json at root) — the deliverable
 *   /Apps/<id>/manifest.json  extracted manifest
 *   /Apps/<id>/code/lib<id>.so  native code (dlopen target)
 *
 * The loader validates the manifest and the ZIP entries, then dlopen()s
 * lib*.so and dlsym()s `q_apps` (a `hachimi_app_module_t` array head) or
 * `q_create` (a `lv_obj_t *(*)(lv_obj_t*)` factory). On a static PID1
 * (musl -static) dlopen cannot work; the loader then reports that
 * limitation instead of crashing.
 *
 * On-host (preview/Linux) dlopen works; the smoke test in
 * homo/tests/app_loader_smoke.c proves it with a tiny .so.
 */

/* Try to load one extracted app directory. Returns the module pointer on
 * success (caller owns the dlopen handle via the returned descriptor's
 * module->id pointing at the handle), or NULL with *err set. */
const hachimi_app_module_t *hachimi_app_loader_load(const char *dir_path, const char **err_out);

bool hachimi_app_loader_is_dynamic(void);

#endif
