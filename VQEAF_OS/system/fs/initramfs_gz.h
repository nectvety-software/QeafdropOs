/* SPDX-License-Identifier: MIT */
/* fs/initramfs_gz.h */

#ifndef UMOS_INITRAMFS_GZ_H
#define UMOS_INITRAMFS_GZ_H

#include <stdint.h>
#include <stddef.h>

long initramfs_gz_load(const uint8_t *gz_data, size_t gz_len,
                      uint8_t *out_buf, size_t out_cap);
int initramfs_gz_register(const uint8_t *cpio_data, size_t cpio_len);

#endif
