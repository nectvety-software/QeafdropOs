/* SPDX-License-Identifier: MIT */
/* core/memory/heap.c - UMOS simple bump allocator */

#include "heap.h"
#include <stdint.h>

static uint8_t *heap_start = NULL;
static size_t heap_size = 0;
static size_t heap_used = 0;
static size_t heap_high = 0;

int umos_heap_init(void *region, size_t size) {
    heap_start = (uint8_t *)region;
    heap_size = size;
    heap_used = 0;
    heap_high = 0;
    return 0;
}

void *umos_heap_alloc(size_t size) {
    if (heap_used + size > heap_size) return NULL;
    void *p = heap_start + heap_used;
    heap_used += size;
    if (heap_used > heap_high) heap_high = heap_used;
    return p;
}

void umos_heap_free(void *p) {
    (void)p; /* bump allocator: no free */
}

void *umos_heap_realloc(void *p, size_t size) {
    (void)p;
    return umos_heap_alloc(size);
}

size_t umos_heap_used(void) { return heap_used; }
size_t umos_heap_free_bytes(void) { return heap_size - heap_used; }
size_t umos_heap_total(void) { return heap_size; }
size_t umos_heap_high_water(void) { return heap_high; }
