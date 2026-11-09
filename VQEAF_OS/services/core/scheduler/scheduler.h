/* SPDX-License-Identifier: MIT */
/* core/scheduler.h - UMOS cooperative scheduler */

#ifndef UMOS_SCHEDULER_H
#define UMOS_SCHEDULER_H

#include <stdint.h>
#include <stddef.h>

#define UMOS_SCHED_TASKS 16

typedef void (*umos_task_fn)(void *arg);

struct umos_task {
    const char *name;
    umos_task_fn fn;
    void *arg;
    uint32_t period_ms;
    uint32_t next_run_ms;
    int active;
};

int umos_sched_add(const char *name, umos_task_fn fn, void *arg,
                   uint32_t period_ms, uint32_t start_ms);
void umos_sched_tick(unsigned now_ms);
void umos_sched_clear(void);
int umos_sched_next_in(unsigned now_ms);
int umos_sched_tasks(void);

#endif
