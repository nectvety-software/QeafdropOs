/* SPDX-License-Identifier: MIT */
/* core/log.c - UMOS kernel log ring buffer */

#include "log.h"

static uint8_t log_buf[UMOS_LOG_SIZE];
static size_t log_head = 0;
static size_t log_tail = 0;
static int log_inited = 0;

static void log_putc(char c) {
    log_buf[log_head++] = (uint8_t)c;
    if (log_head >= UMOS_LOG_SIZE) log_head = 0;
    if (log_head == log_tail) {
        log_tail++;
        if (log_tail >= UMOS_LOG_SIZE) log_tail = 0;
    }
}

void umos_log_init(void) {
    log_head = 0;
    log_tail = 0;
    log_inited = 1;
}

void umos_logf(const char *fmt, ...) {
    (void)fmt;
    /* Minimal: just a placeholder, real formatting in stdio */
}

void umos_log_raw(const char *s, size_t len) {
    for (size_t i = 0; i < len; i++) {
        log_putc(s[i]);
    }
}

size_t umos_log_read(uint8_t *buf, size_t len, size_t off) {
    size_t count = 0;
    size_t pos = log_tail + off;
    if (pos >= UMOS_LOG_SIZE) pos -= UMOS_LOG_SIZE;
    while (count < len && pos != log_head) {
        buf[count++] = log_buf[pos++];
        if (pos >= UMOS_LOG_SIZE) pos = 0;
    }
    return count;
}
