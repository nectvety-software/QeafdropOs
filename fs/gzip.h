/* SPDX-License-Identifier: MIT */
/* fs/gzip.h - Minimal gzip (.gz) header parser + inflate stub */

#ifndef UMOS_GZIP_H
#define UMOS_GZIP_H

#include <stdint.h>
#include <stddef.h>

#define GZIP_MAGIC0 0x1F
#define GZIP_MAGIC1 0x8B
#define GZIP_METHOD_DEFLATE 8

struct gzip_header {
    uint8_t magic0;
    uint8_t magic1;
    uint8_t method;
    uint8_t flags;
    uint32_t mtime;
    uint8_t xfl;
    uint8_t os;
    uint16_t extra_len;
    uint8_t *extra;
    uint8_t *fname;
    uint8_t *comment;
    uint16_t crc16;
};

/* Returns 0 if valid gzip, fills out header. -1 on error. */
int gzip_parse_header(const uint8_t *data, size_t len, struct gzip_header *out);

/* Decompress gzip data in-place. out_buf must be pre-allocated.
 * Returns decompressed size, or -1 on error. */
long gzip_decompress(const uint8_t *in, size_t in_len,
                        uint8_t *out, size_t out_cap);

#endif
