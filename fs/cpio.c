/* SPDX-License-Identifier: MIT */
/* fs/cpio.c - Newc cpio archive reader */

#include "initramfs.h"
#include <string.h>

static const uint8_t *g_cpio_image = NULL;
static size_t g_cpio_size = 0;

static uint32_t cpio_hex_to_uint(const char *s, int len) {
    uint32_t v = 0;
    for (int i = 0; i < len; i++) {
        char c = s[i];
        if (c >= '0' && c <= '9') v = (v << 4) | (c - '0');
        else if (c >= 'a' && c <= 'f') v = (v << 4) | (c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v = (v << 4) | (c - 'A' + 10);
    }
    return v;
}

int initramfs_init(const void *image, size_t size) {
    g_cpio_image = (const uint8_t *)image;
    g_cpio_size = size;
    return 0;
}

int initramfs_find_file(const char *name, uint8_t **data, size_t *size) {
    if (!g_cpio_image) return -1;
    const uint8_t *p = g_cpio_image;
    size_t off = 0;

    while (off + sizeof(struct cpio_newc_header) <= g_cpio_size) {
        struct cpio_newc_header *hdr = (struct cpio_newc_header *)(p + off);
        if (memcmp(hdr->magic, CPIO_NEWC_MAGIC, 6) != 0) break;

        uint32_t namesize = cpio_hex_to_uint(hdr->namesize, 8);
        uint32_t filesize = cpio_hex_to_uint(hdr->filesize, 8);

        off += sizeof(struct cpio_newc_header);

        /* Align namesize to 4 bytes */
        uint32_t name_pad = (namesize + 3) & ~3;
        const char *fname = (const char *)(p + off);
        off += name_pad;

        /* Check for trailer */
        if (memcmp(fname, CPIO_NEWC_TRAILER, 11) == 0) break;

        if (strcmp(fname, name) == 0) {
            if (data) *data = (uint8_t *)(p + off);
            if (size) *size = filesize;
            return 0;
        }

        /* Align filesize to 4 bytes */
        uint32_t data_pad = (filesize + 3) & ~3;
        off += data_pad;
    }

    return -1;
}

int initramfs_list(void (*cb)(const char *name, size_t size, void *arg), void *arg) {
    if (!g_cpio_image || !cb) return -1;
    const uint8_t *p = g_cpio_image;
    size_t off = 0;

    while (off + sizeof(struct cpio_newc_header) <= g_cpio_size) {
        struct cpio_newc_header *hdr = (struct cpio_newc_header *)(p + off);
        if (memcmp(hdr->magic, CPIO_NEWC_MAGIC, 6) != 0) break;

        uint32_t namesize = cpio_hex_to_uint(hdr->namesize, 8);
        uint32_t filesize = cpio_hex_to_uint(hdr->filesize, 8);

        off += sizeof(struct cpio_newc_header);

        uint32_t name_pad = (namesize + 3) & ~3;
        const char *fname = (const char *)(p + off);
        off += name_pad;

        if (memcmp(fname, CPIO_NEWC_TRAILER, 11) == 0) break;

        cb(fname, filesize, arg);

        uint32_t data_pad = (filesize + 3) & ~3;
        off += data_pad;
    }

    return 0;
}
