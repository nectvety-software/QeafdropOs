/* SPDX-License-Identifier: MIT */
/* core/memory/heap.h - UMOS simple heap */

#ifndef UMOS_HEAP_H
#define UMOS_HEAP_H

#include <stddef.h>

int umos_heap_init(void *region, size_t size);
void *umos_heap_alloc(size_t size);
void umos_heap_free(void *p);
void *umos_heap_realloc(void *p, size_t size);
size_t umos_heap_used(void);
size_t umos_heap_free_bytes(void);
size_t umos_heap_total(void);
size_t umos_heap_high_water(void);

#endif
