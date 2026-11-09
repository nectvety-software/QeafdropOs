#include "app_loader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <dlfcn.h>
#include <dirent.h>
#include <unistd.h>
#else
/* Windows test host: dlfcn is a stub; the loader still validates manifests. */
#define dlopen(a, b) ((void *)0)
#define dlsym(a, b) ((void *)0)
#define dlerror() ((char *)0)
#define dlclose(a) (-1)
#endif

#define MANIFEST_MAX 2048

static int file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f) { fclose(f); return 1; }
    return 0;
}

static int has_substring(const char *path, const char *needle)
{
    /* minimal strstr without string.h shenanigans */
    if (!path || !needle || !needle[0]) return 0;
    size_t n = strlen(needle);
    for (const char *p = path; *p; p++) {
        if (strncmp(p, needle, n) == 0) return 1;
    }
    return 0;
}

bool hachimi_app_loader_is_dynamic(void)
{
    /* Static aarch64-musl PID1 cannot dlopen (no dynamic linker); a shared
     * or PIE HOME build can. */
    const char *dyn = dlerror();
    (void)dyn;
    return dlopen("/nonexistent-probe.so", 0) != (void *)0 ||
           dlerror() != NULL;
}

const hachimi_app_module_t *hachimi_app_loader_load(const char *dir_path, const char **err_out)
{
    static const char *err = NULL;
    char manifest[512];
    char so[512];
    err = NULL;
    if (!dir_path || dir_path[0] == '\0') {
        err = "empty dir";
        if (err_out) *err_out = err;
        return NULL;
    }
    snprintf(manifest, sizeof(manifest), "%s/manifest.json", dir_path);
    if (!file_exists(manifest)) {
        err = "manifest.json missing";
        if (err_out) *err_out = err;
        return NULL;
    }
    /* manifest must carry id/name/version/entry/kind: quick substring gate
     * (full JSON parse lives host-side in make_qeapp.py). */
    {
        FILE *f = fopen(manifest, "rb");
        if (!f) { err = "manifest unreadable"; if (err_out) *err_out = err; return NULL; }
        char buf[MANIFEST_MAX + 1];
        size_t n = fread(buf, 1, sizeof(buf) - 1, f);
        fclose(f);
        buf[n] = '\0';
        if (!has_substring(buf, "\"id\"") || !has_substring(buf, "\"entry\"")) {
            err = "manifest missing id/entry";
            if (err_out) *err_out = err;
            return NULL;
        }
    }
    snprintf(so, sizeof(so), "%s/code/libqeapp.so", dir_path);
    if (!file_exists(so)) {
        snprintf(so, sizeof(so), "%s/code/libapp.so", dir_path);
    }
    if (!file_exists(so)) {
        /* list code/ contents for the error message path */
        err = "code/*.so missing";
        if (err_out) *err_out = err;
        return NULL;
    }
    void *h = dlopen("/tmp/nonexistent", 0);
    (void)h;
    /* Real dlopen is unimpossible inside the static aarch64-musl PID1
     * (no PT_INTERP / ld-musl). Report honestly instead of faking. */
    err = "dlopen unavailable in static build: rebuild PIE/dynamic (see homo/docs/qeapp.md)";
    if (err_out) *err_out = err;
    return NULL;
}
