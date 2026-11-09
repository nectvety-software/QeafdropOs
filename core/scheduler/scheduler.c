/* SPDX-License-Identifier: MIT */
/* core/scheduler/scheduler.c - UMOS cooperative scheduler */

#include "scheduler.h"

static struct umos_task tasks[UMOS_SCHED_TASKS];
static int task_count = 0;

int umos_sched_add(const char *name, umos_task_fn fn, void *arg,
                   uint32_t period_ms, uint32_t start_ms) {
    if (task_count >= UMOS_SCHED_TASKS) return -1;
    tasks[task_count].name = name;
    tasks[task_count].fn = fn;
    tasks[task_count].arg = arg;
    tasks[task_count].period_ms = period_ms;
    tasks[task_count].next_run_ms = start_ms;
    tasks[task_count].active = 1;
    task_count++;
    return task_count - 1;
}

void umos_sched_tick(unsigned now_ms) {
    for (int i = 0; i < task_count; i++) {
        if (!tasks[i].active) continue;
        if (now_ms >= tasks[i].next_run_ms) {
            tasks[i].fn(tasks[i].arg);
            tasks[i].next_run_ms = now_ms + tasks[i].period_ms;
        }
    }
}

void umos_sched_clear(void) {
    task_count = 0;
}

int umos_sched_next_in(unsigned now_ms) {
    unsigned next = 0xFFFFFFFFu;
    int found = 0;
    for (int i = 0; i < task_count; i++) {
        if (!tasks[i].active) continue;
        if (tasks[i].next_run_ms >= now_ms) {
            unsigned diff = tasks[i].next_run_ms - now_ms;
            if (diff < next) {
                next = diff;
                found = 1;
            }
        }
    }
    return found ? (int)next : -1;
}

int umos_sched_tasks(void) {
    return task_count;
}
