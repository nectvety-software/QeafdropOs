/* SPDX-License-Identifier: MIT */
/* core/event_queue.c - UMOS event queue */

#include "event_queue.h"

static struct umos_event eq[UMOS_EVENT_QUEUE_SIZE];
static size_t eq_head = 0;
static size_t eq_tail = 0;
static size_t eq_count = 0;

void umos_eventq_init(void) {
    eq_head = 0;
    eq_tail = 0;
    eq_count = 0;
}

int umos_eventq_push(const struct umos_event *ev) {
    if (eq_count >= UMOS_EVENT_QUEUE_SIZE) return -1;
    eq[eq_head] = *ev;
    eq_head = (eq_head + 1) % UMOS_EVENT_QUEUE_SIZE;
    eq_count++;
    return 0;
}

int umos_eventq_pop(struct umos_event *ev) {
    if (eq_count == 0) return -1;
    *ev = eq[eq_tail];
    eq_tail = (eq_tail + 1) % UMOS_EVENT_QUEUE_SIZE;
    eq_count--;
    return 0;
}
