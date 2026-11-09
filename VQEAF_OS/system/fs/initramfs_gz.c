/* SPDX-License-Identifier: MIT */
/* fs/initramfs_gz.c - Extract cpio archive from .cpio.gz initramfs image */

#include "initramfs.h"
#include "gzip.h"
#include <string.h>

/* Decompress a gzip-compressed cpio archive into an output buffer.
 * Returns decompressed size or -1 on error. */
long initramfs_gz_load(const uint8_t *gz_data, size_t gz_len,
                      uint8_t *out_buf, size_t out_cap) {
    long dec = gzip_decompress(gz_data, gz_len, out_buf, out_cap);
    if (dec < 0) return -1;
    return dec;
}

/* Parse cpio newc archive from decompressed buffer and register files.
 * This is a thin wrapper over initramfs_init/initramfs_find_file. */
int initramfs_gz_register(const uint8_t *cpio_data, size_t cpio_len) {
    return initramfs_init(cpio_data, cpio_len);
}
