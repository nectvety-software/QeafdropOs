/* SPDX-License-Identifier: MIT */
/* fs/fstab.h - Filesystem mount table */

#ifndef UMOS_FSTAB_H
#define UMOS_FSTAB_H

#include <stddef.h>

#define FSTAB_MAX_ENTRIES 8

struct fstab_entry {
    char device[32];
    char mountpoint[32];
    char fstype[16];
    char options[64];
    int mounted;
};

int fstab_init(void);
int fstab_add(const char *device, const char *mountpoint,
              const char *fstype, const char *options);
int fstab_mount_all(void);
struct fstab_entry *fstab_find(const char *mountpoint);

#endif
