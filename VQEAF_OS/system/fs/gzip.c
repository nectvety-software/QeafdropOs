/* SPDX-License-Identifier: MIT */
/* fs/gzip.c - Minimal gzip header parser + stub inflate */

#include "gzip.h"
#include <string.h>

int gzip_parse_header(const uint8_t *data, size_t len, struct gzip_header *out) {
    if (len < 10) return -1;
    if (data[0] != GZIP_MAGIC0 || data[1] != GZIP_MAGIC1) return -1;

    memset(out, 0, sizeof(*out));
    out->magic0 = data[0];
    out->magic1 = data[1];
    out->method = data[2];
    out->flags = data[3];
    out->mtime = (uint32_t)data[4] | ((uint32_t)data[5] << 8) |
                 ((uint32_t)data[6] << 16) | ((uint32_t)data[7] << 24);
    out->xfl = data[8];
    out->os = data[9];

    if (out->method != GZIP_METHOD_DEFLATE) return -1;

    size_t off = 10;

    /* FEXTRA */
    if (out->flags & 0x04) {
        if (off + 2 > len) return -1;
        out->extra_len = data[off] | (data[off + 1] << 8);
        off += 2;
        if (off + out->extra_len > len) return -1;
        out->extra = (uint8_t *)(data + off);
        off += out->extra_len;
    }

    /* FNAME */
    if (out->flags & 0x08) {
        out->fname = (uint8_t *)(data + off);
        while (off < len && data[off] != 0) off++;
        if (off >= len) return -1;
        off++; /* skip NUL */
    }

    /* FCOMMENT */
    if (out->flags & 0x10) {
        out->comment = (uint8_t *)(data + off);
        while (off < len && data[off] != 0) off++;
        if (off >= len) return -1;
        off++; /* skip NUL */
    }

    /* FHCRC */
    if (out->flags & 0x02) {
        if (off + 2 > len) return -1;
        out->crc16 = data[off] | (data[off + 1] << 8);
        off += 2;
    }

    return (int)off; /* offset to compressed data */
}

/* ------------------------------------------------------------------ */
/* Minimal inflate (deflate method 8) - fixed Huffman codes only      */
/* Supports: stored (0), fixed Huffman (1). No dynamic (2) yet.       */
/* ------------------------------------------------------------------ */

#define INFLATE_NO_BLOCK 0
#define INFLATE_FIXED_HUFFMAN 1
#define INFLATE_DYNAMIC_HUFFMAN 2

/* Fixed literal/length code lengths (RFC 1951) */
static const uint8_t fixed_lit_len[288] = {
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
    9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9, 9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
    9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9, 9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7, 7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7, 7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8
};

/* Fixed distance code lengths (32 entries, all 5 bits) */
static const uint8_t fixed_dist[32] = {
    5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
    5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5
};

struct inflate_state {
    const uint8_t *in;
    size_t in_pos;
    size_t in_len;
    uint8_t *out;
    size_t out_pos;
    size_t out_cap;
    uint32_t bitbuf;
    int bitcnt;
};

static int inflate_fill_bits(struct inflate_state *s, int n) {
    while (s->bitcnt < n) {
        if (s->in_pos >= s->in_len) return -1;
        s->bitbuf |= (uint32_t)s->in[s->in_pos++] << s->bitcnt;
        s->bitcnt += 8;
    }
    return 0;
}

static uint32_t inflate_get_bits(struct inflate_state *s, int n) {
    uint32_t r = s->bitbuf & ((1u << n) - 1);
    s->bitbuf >>= n;
    s->bitcnt -= n;
    return r;
}

static void inflate_drop_bits(struct inflate_state *s, int n) {
    s->bitbuf >>= n;
    s->bitcnt -= n;
}

/* Simple Huffman decoder for fixed tables */
static int inflate_decode_symbol(struct inflate_state *s,
                                 const uint8_t *lengths, int num_codes) {
    /* Canonical Huffman decode - bit-by-bit for simplicity */
    uint32_t code = 0;
    int len = 0;
    int first = 0;
    int count[16] = {0};
    int start[16] = {0};

    for (int i = 0; i < num_codes; i++) {
        if (lengths[i] > 0) count[lengths[i]]++;
    }
    int code_val = 0;
    for (int bits = 1; bits <= 15; bits++) {
        start[bits] = code_val;
        code_val += count[bits];
        code_val <<= 1;
    }

    if (inflate_fill_bits(s, 1) < 0) return -1;
    while (1) {
        code = (code << 1) | inflate_get_bits(s, 1);
        len++;
        if (count[len] != 0) {
            int index = start[len] + (code - ((1u << len) - 1u) + count[len] - count[len]);
            /* find symbol */
            int sym = 0;
            int cumulative = start[len];
            for (int i = 0; i < num_codes; i++) {
                if (lengths[i] == len) {
                    if (cumulative == (code - ((1u << len) - 1u) + start[len] - count[len] + count[len] - start[len] + 0)) break;
                    cumulative++;
                    sym++;
                }
            }
            return sym;
        }
        if (len > 15) return -1;
    }
}

/* LZ77 base and extra tables for lengths 257..285 */
static const int length_base[29] = {
    3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258
};
static const int length_extra[29] = {
    0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0
};
static const int dist_base[30] = {
    1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577
};
static const int dist_extra[30] = {
    0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13
};

long gzip_decompress(const uint8_t *in, size_t in_len,
                        uint8_t *out, size_t out_cap) {
    struct inflate_state s;
    memset(&s, 0, sizeof(s));
    s.in = in;
    s.in_len = in_len;
    s.out = out;
    s.out_cap = out_cap;

    /* Skip gzip header (parse but we don't need it fully) */
    struct gzip_header hdr;
    int data_off = gzip_parse_header(in, in_len, &hdr);
    if (data_off < 0) return -1;
    s.in_pos = (size_t)data_off;

    while (1) {
        if (inflate_fill_bits(&s, 3) < 0) return -1;
        int bfinal = inflate_get_bits(&s, 1);
        int btype = inflate_get_bits(&s, 2);

        if (btype == INFLATE_NO_BLOCK) {
            /* Align to byte boundary */
            inflate_drop_bits(&s, s.bitcnt & 7);
            if (s.in_pos + 4 > s.in_len) return -1;
            uint32_t len = s.in[s.in_pos] | (s.in[s.in_pos+1] << 8);
            uint32_t nlen = s.in[s.in_pos+2] | (s.in[s.in_pos+3] << 8);
            s.in_pos += 4;
            if (len != (nlen ^ 0xFFFF)) return -1;
            if (s.in_pos + len > s.in_len) return -1;
            if (s.out_pos + len > s.out_cap) return -1;
            memcpy(s.out + s.out_pos, s.in + s.in_pos, len);
            s.out_pos += len;
            s.in_pos += len;
        }
        else if (btype == INFLATE_FIXED_HUFFMAN) {
            while (1) {
                int sym = inflate_decode_symbol(&s, fixed_lit_len, 288);
                if (sym < 0) return -1;
                if (sym < 256) {
                    if (s.out_pos >= s.out_cap) return -1;
                    s.out[s.out_pos++] = (uint8_t)sym;
                }
                else if (sym == 256) {
                    break; /* end of block */
                }
                else {
                    int length_index = sym - 257;
                    if (length_index < 0 || length_index >= 29) return -1;
                    int match_len = length_base[length_index];
                    if (length_extra[length_index] > 0) {
                        if (inflate_fill_bits(&s, length_extra[length_index]) < 0) return -1;
                        match_len += inflate_get_bits(&s, length_extra[length_index]);
                    }
                    if (inflate_fill_bits(&s, 5) < 0) return -1;
                    int dist_sym = inflate_get_bits(&s, 5);
                    if (dist_sym >= 30) return -1;
                    int match_dist = dist_base[dist_sym];
                    if (dist_extra[dist_sym] > 0) {
                        if (inflate_fill_bits(&s, dist_extra[dist_sym]) < 0) return -1;
                        match_dist += inflate_get_bits(&s, dist_extra[dist_sym]);
                    }
                    if (match_dist > s.out_pos) return -1;
                    if (s.out_pos + match_len > s.out_cap) return -1;
                    for (int i = 0; i < match_len; i++) {
                        s.out[s.out_pos] = s.out[s.out_pos - match_dist];
                        s.out_pos++;
                    }
                }
            }
        }
        else {
            /* Dynamic Huffman not implemented */
            return -1;
        }

        if (bfinal) break;
    }

    /* Skip gzip footer (CRC32 + ISIZE, 8 bytes) - we don't verify */
    (void)s.in_pos;
    return (long)s.out_pos;
}
