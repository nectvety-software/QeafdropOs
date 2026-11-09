/* SPDX-License-Identifier: MIT */
/* core/prefs.c - flat key=value preferences in /etc/qeaf.conf plus a durable
 * copy in the tail of p77.
 *
 * The caller owns validation against its own enums (the UI applies
 * i18n_set()/display_set_orientation()); this file only guarantees that what
 * it writes can be parsed back, that missing keys leave the caller's value
 * alone, and that a bad number never becomes a valid-looking preference.
 *
 * The record in p77 is written with header + CRC and read back byte for byte
 * before it is believed. A corrupt record falls back to /etc/qeaf.conf (or to
 * the caller's defaults), so a half-written slot can never invent a setting.
 */

#include "core/prefs.h"
#include "core/sysio.h"
#include "core/log.h"

#include <stddef.h>
#include <stdint.h>

#define PREFS_PATH_MAX 128
#define PREFS_LANG_MAX 8          /* i18n_lang: VI..AUTO */
#define PREFS_ROT_MAX  4          /* display_orientation: 0..3 */

static char g_path[PREFS_PATH_MAX] = PREFS_DEFAULT_PATH;
static char g_block_path[PREFS_PATH_MAX];
static size_t g_block_offset;
static int g_block_on;
static int g_source;              /* 0 none, 1 p77 slot, 2 /etc file */
static int g_have_cache;
static int g_cache_lang;
static int g_cache_rot;
static int g_cache_auto;

static void copy_path(char *dst, size_t cap, const char *src) {
    size_t n = 0;
    if (!dst || cap == 0) return;
    while (src && src[n] && n + 1 < cap) {
        dst[n] = src[n];
        n++;
    }
    dst[n] = '\0';
}

void prefs_set_path(const char *path) {
    if (!path || !path[0]) return;
    copy_path(g_path, sizeof(g_path), path);
    g_have_cache = 0;
    g_source = 0;
}

void prefs_set_block(const char *path, size_t offset) {
    if (!path || !path[0]) {
        g_block_on = 0;
        g_block_path[0] = '\0';
    } else {
        copy_path(g_block_path, sizeof(g_block_path), path);
        g_block_offset = offset;
        g_block_on = 1;
    }
    g_have_cache = 0;
    g_source = 0;
}

const char *prefs_source(void) {
    if (g_source == 1) return "p77 slot";
    if (g_source == 2) return "/etc/qeaf.conf";
    return "none";
}

/* Strict unsigned decimal: no sign, no trailing junk, max three digits. */
static int parse_int(const char *s, int *out) {
    int v = 0, digits = 0;
    while (*s == ' ' || *s == '\t') s++;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        digits++;
        if (digits > 3) return 0;
        s++;
    }
    while (*s == ' ' || *s == '\t' || *s == '\r') s++;
    /* The parser walks a buffer, not a C string cut at the line end: a newline
     * (or the buffer's NUL) is the only acceptable terminator here. */
    if (!digits || (*s != '\0' && *s != '\n')) return 0;
    *out = v;
    return 1;
}

static int key_is(const char *line, int len, const char *key) {
    int i = 0;
    while (key[i]) {
        if (i >= len || line[i] != key[i]) return 0;
        i++;
    }
    return i == len;
}

/* Parse the same text from either backend. `any` reports whether at least one
 * recognised key passed its range check. */
static int parse_payload(const char *buf, int n, int *language, int *orientation,
                         int *auto_rotate) {
    int pos = 0, any = 0;

    while (pos < n) {
        int end = pos;
        int eq = -1, i;
        int value;

        while (end < n && buf[end] != '\n') end++;
        for (i = pos; i < end; i++) {
            if (buf[i] == '=') { eq = i; break; }
        }
        if (eq > pos && parse_int(buf + eq + 1, &value)) {
            int klen = eq - pos;
            if (key_is(buf + pos, klen, "language")) {
                if (value >= 0 && value < PREFS_LANG_MAX) {
                    *language = value;
                    any = 1;
                }
            } else if (key_is(buf + pos, klen, "orientation")) {
                if (value >= 0 && value < PREFS_ROT_MAX) {
                    *orientation = value;
                    any = 1;
                }
            } else if (key_is(buf + pos, klen, "auto_rotate")) {
                if (value == 0 || value == 1) {
                    *auto_rotate = value;
                    any = 1;
                }
            }
        }
        pos = end + 1;
    }
    return any;
}

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void put_le32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xff);
    p[1] = (uint8_t)((v >> 8) & 0xff);
    p[2] = (uint8_t)((v >> 16) & 0xff);
    p[3] = (uint8_t)((v >> 24) & 0xff);
}

/* Bitwise CRC32 (IEEE polynomial). The payload is ~40 bytes, so a table would
 * cost more flash than it saves. */
static uint32_t crc32(const uint8_t *p, size_t n) {
    uint32_t c = 0xffffffffu;
    size_t i;
    int bit;
    for (i = 0; i < n; i++) {
        c ^= p[i];
        for (bit = 0; bit < 8; bit++)
            c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1u)));
    }
    return c ^ 0xffffffffu;
}

/* Read and validate the p77 record. On success the payload is copied into
 * `text` (NUL-terminated) and its length returned; 0 means no usable record. */
static int block_read_text(char *text, int cap) {
    uint8_t rec[PREFS_BLOCK_MAX];
    uint32_t len, crc;
    int i;

    if (!g_block_on || cap <= 1) return 0;
    if (sysio_block_read_at(g_block_path, g_block_offset, rec,
                            sizeof(rec)) != 0)
        return 0;
    if (rec[0] != 'Q' || rec[1] != 'E' || rec[2] != 'A' || rec[3] != 'F' ||
        rec[4] != 'P' || rec[5] != 'R' || rec[6] != 'F' || rec[7] != '1')
        return 0;
    if (le32(rec + 8) != PREFS_BLOCK_VERSION) return 0;
    len = le32(rec + 12);
    crc = le32(rec + 16);
    if (len == 0 || len > PREFS_BLOCK_MAX - PREFS_BLOCK_HEADER ||
        len > (uint32_t)(cap - 1))
        return 0;
    if (crc32(rec + PREFS_BLOCK_HEADER, len) != crc) return 0;
    for (i = 0; i < (int)len; i++)
        text[i] = (char)rec[PREFS_BLOCK_HEADER + i];
    text[len] = '\0';
    return (int)len;
}

/* Build, write and read back the record. Returns 0 only when the bytes that
 * came back are the bytes that went out. */
static int block_write_text(const char *text, int len) {
    uint8_t rec[PREFS_BLOCK_MAX];
    uint8_t back[PREFS_BLOCK_MAX];
    size_t total;
    int i;

    if (!g_block_on) return -1;
    if (len <= 0 || len > (int)(PREFS_BLOCK_MAX - PREFS_BLOCK_HEADER))
        return -1;
    for (i = 0; i < (int)PREFS_BLOCK_HEADER; i++) rec[i] = 0;
    rec[0] = 'Q'; rec[1] = 'E'; rec[2] = 'A'; rec[3] = 'F';
    rec[4] = 'P'; rec[5] = 'R'; rec[6] = 'F'; rec[7] = '1';
    put_le32(rec + 8, PREFS_BLOCK_VERSION);
    put_le32(rec + 12, (uint32_t)len);
    put_le32(rec + 16, crc32((const uint8_t *)text, (size_t)len));
    for (i = 0; i < len; i++)
        rec[PREFS_BLOCK_HEADER + i] = (uint8_t)text[i];
    total = PREFS_BLOCK_HEADER + (size_t)len;

    if (sysio_block_write_at(g_block_path, g_block_offset, rec, total) != 0)
        return -1;
    if (sysio_block_read_at(g_block_path, g_block_offset, back, total) != 0)
        return -1;
    for (i = 0; i < (int)total; i++)
        if (back[i] != rec[i]) return -1;
    return 0;
}

int prefs_load(int *language, int *orientation, int *auto_rotate) {
    char buf[192];
    int n, any = 0;
    int lang = language ? *language : 0;
    int rot = orientation ? *orientation : 0;
    int aut = auto_rotate ? *auto_rotate : 0;

    /* The durable copy wins: it is the one a reboot writes over /etc. */
    n = block_read_text(buf, (int)sizeof(buf));
    if (n > 0) {
        any = parse_payload(buf, n, &lang, &rot, &aut);
        if (any) g_source = 1;
    }
    if (!any) {
        n = sysio_read_file(g_path, buf, (int)sizeof(buf));
        if (n > 0) {
            any = parse_payload(buf, n, &lang, &rot, &aut);
            if (any) g_source = 2;
        }
    }

    if (language) *language = lang;
    if (orientation) *orientation = rot;
    if (auto_rotate) *auto_rotate = aut;

    /* Seed the cache even for keys the file omitted: the caller's defaults are
     * now the live state, and boot must not immediately rewrite the file. */
    g_cache_lang = lang;
    g_cache_rot = rot;
    g_cache_auto = aut;
    g_have_cache = 1;
    return any ? 0 : -1;
}

static int put(char *buf, int cap, int at, const char *text) {
    int i = 0;
    while (text[i]) {
        if (at >= cap - 1) return -1;
        buf[at++] = text[i++];
    }
    buf[at] = '\0';
    return at;
}

static int put_int(char *buf, int cap, int at, int value) {
    char digits[4];
    int n = 0;
    if (at < 0) return -1;
    do { digits[n++] = (char)('0' + value % 10); value /= 10; } while (value);
    while (n) {
        if (at >= cap - 1) return -1;
        buf[at++] = digits[--n];
    }
    buf[at] = '\0';
    return at;
}

int prefs_save(int language, int orientation, int auto_rotate) {
    char buf[128];
    int at = 0;
    int file_rc, block_rc = -1;

    if (language < 0 || language >= PREFS_LANG_MAX) return -1;
    if (orientation < 0 || orientation >= PREFS_ROT_MAX) return -1;
    if (auto_rotate < 0 || auto_rotate > 1) return -1;

    /* The root is an initramfs and may not carry an empty /etc yet. On the
     * host the seam is a no-op, so a gate never creates a Windows C:\etc. */
    sysio_mkdir_p("/etc");

    at = put(buf, (int)sizeof(buf), at, "language=");
    at = put_int(buf, (int)sizeof(buf), at, language);
    at = put(buf, (int)sizeof(buf), at, "\norientation=");
    at = put_int(buf, (int)sizeof(buf), at, orientation);
    at = put(buf, (int)sizeof(buf), at, "\nauto_rotate=");
    at = put_int(buf, (int)sizeof(buf), at, auto_rotate);
    at = put(buf, (int)sizeof(buf), at, "\n");
    if (at < 0) return -1;

    file_rc = sysio_write_str(g_path, buf);
    if (g_block_on) {
        block_rc = block_write_text(buf, at);
        if (block_rc != 0)
            umos_logf("prefs: p77 slot write/readback failed; kept the "
                      "session copy only\n");
    }

    /* Either durable copy is enough to call the save good; the file is the one
     * the running session reads, the slot is the one the next boot reads. */
    if (file_rc == 0 || block_rc == 0) {
        g_cache_lang = language;
        g_cache_rot = orientation;
        g_cache_auto = auto_rotate;
        g_have_cache = 1;
        g_source = block_rc == 0 ? 1 : 2;
        return 0;
    }
    return file_rc;
}

int prefs_sync(int language, int orientation, int auto_rotate) {
    if (g_have_cache && language == g_cache_lang &&
        orientation == g_cache_rot && auto_rotate == g_cache_auto) {
        return 0;
    }
    return prefs_save(language, orientation, auto_rotate) == 0 ? 1 : -1;
}
