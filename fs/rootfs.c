/* SPDX-License-Identifier: MIT */
/* fs/rootfs.c - Simple rootfs mount over initramfs */

#include "fstab.h"
#include "rootfs.h"
#include <string.h>

static int g_rootfs_mounted = 0;
static char g_rootfs_device[32] = "initramfs";

int rootfs_init(const char *mountpoint) {
    if (g_rootfs_mounted) return 0;
    /* Register initramfs as a mountable device */
    fstab_add(g_rootfs_device, mountpoint, "initramfs", "ro");
    g_rootfs_mounted = 1;
    return 0;
}

int rootfs_mount_all(void) {
    struct fstab_entry *e = fstab_find("/");
    if (!e) e = fstab_find(g_rootfs_device);
    if (e) e->mounted = 1;
    return 0;
}

int rootfs_is_mounted(void) {
    return g_rootfs_mounted;
}

const char *rootfs_device(void) {
    return g_rootfs_device;
}
