/* SPDX-License-Identifier: MIT */
/* fs/rootfs.h */

#ifndef UMOS_ROOTFS_H
#define UMOS_ROOTFS_H

#include <stddef.h>

int rootfs_init(const char *mountpoint);
int rootfs_mount_all(void);
int rootfs_is_mounted(void);
const char *rootfs_device(void);

#endif
