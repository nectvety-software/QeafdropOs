/* SPDX-License-Identifier: MIT */
/* fs/fstab.c - fstab mount table */

#include "fstab.h"
#include <string.h>

static struct fstab_entry g_entries[FSTAB_MAX_ENTRIES];
static int g_count = 0;

int fstab_init(void) {
    g_count = 0;
    return 0;
}

int fstab_add(const char *device, const char *mountpoint,
              const char *fstype, const char *options) {
    if (g_count >= FSTAB_MAX_ENTRIES) return -1;
    struct fstab_entry *e = &g_entries[g_count++];
    strncpy(e->device, device, sizeof(e->device) - 1);
    e->device[sizeof(e->device) - 1] = '\0';
    strncpy(e->mountpoint, mountpoint, sizeof(e->mountpoint) - 1);
    e->mountpoint[sizeof(e->mountpoint) - 1] = '\0';
    strncpy(e->fstype, fstype, sizeof(e->fstype) - 1);
    e->fstype[sizeof(e->fstype) - 1] = '\0';
    strncpy(e->options, options, sizeof(e->options) - 1);
    e->options[sizeof(e->options) - 1] = '\0';
    e->mounted = 0;
    return 0;
}

int fstab_mount_all(void) {
    /* Stub: actual mounting depends on filesystem drivers */
    for (int i = 0; i < g_count; i++) {
        g_entries[i].mounted = 1;
    }
    return 0;
}

struct fstab_entry *fstab_find(const char *mountpoint) {
    for (int i = 0; i < g_count; i++) {
        if (strcmp(g_entries[i].mountpoint, mountpoint) == 0) return &g_entries[i];
    }
    return NULL;
}
