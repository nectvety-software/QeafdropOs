/* SPDX-License-Identifier: MIT */
/* core/log.h - UMOS kernel log ring buffer */

#ifndef UMOS_LOG_H
#define UMOS_LOG_H

#include <stdint.h>
#include <stddef.h>

#define UMOS_LOG_SIZE 4096

void umos_log_init(void);
void umos_logf(const char *fmt, ...);
void umos_log_raw(const char *s, size_t len);
size_t umos_log_read(uint8_t *buf, size_t len, size_t off);

#endif
