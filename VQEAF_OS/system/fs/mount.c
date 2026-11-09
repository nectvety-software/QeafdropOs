/* SPDX-License-Identifier: MIT */
/* fs/mount.c - Mount operations */

#include "fstab.h"
#include "initramfs.h"
#include <string.h>

int mount_initramfs_to_path(const char *mountpoint, const char *filename) {
    uint8_t *data = NULL;
    size_t size = 0;
    if (initramfs_find_file(filename, &data, &size) != 0) return -1;
    /* In a real system this would extract to a tmpfs or ramfs at mountpoint */
    (void)mountpoint;
    (void)data;
    (void)size;
    return 0;
}
