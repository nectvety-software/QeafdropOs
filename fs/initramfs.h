/* SPDX-License-Identifier: MIT */
/* fs/initramfs.h - initramfs (cpio) support */

#ifndef UMOS_INITRAMFS_H
#define UMOS_INITRAMFS_H

#include <stdint.h>
#include <stddef.h>

/* Newc (crc) cpio header magic */
#define CPIO_NEWC_MAGIC "070701"
#define CPIO_NEWC_TRAILER "TRAILER!!!"

struct cpio_newc_header {
    char magic[6];
    char ino[8];
    char mode[8];
    char uid[8];
    char gid[8];
    char nlink[8];
    char mtime[8];
    char filesize[8];
    char devmajor[8];
    char devminor[8];
    char rdevmajor[8];
    char rdevminor[8];
    char namesize[8];
    char check[8];
} __attribute__((packed));

int initramfs_init(const void *image, size_t size);
int initramfs_find_file(const char *name, uint8_t **data, size_t *size);
int initramfs_list(void (*cb)(const char *name, size_t size, void *arg), void *arg);

#endif
