/* SPDX-License-Identifier: MIT */
/* core/event_queue.h - UMOS event queue */

#ifndef UMOS_EVENT_QUEUE_H
#define UMOS_EVENT_QUEUE_H

#include <stdint.h>
#include <stddef.h>

#define UMOS_EVENT_QUEUE_SIZE 64

struct umos_event {
    uint32_t type;
    uint32_t data1;
    uint32_t data2;
};

void umos_eventq_init(void);
int umos_eventq_push(const struct umos_event *ev);
int umos_eventq_pop(struct umos_event *ev);

#endif
