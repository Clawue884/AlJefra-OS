/* SPDX-License-Identifier: MIT */
/* AlJefra OS — Scheduler Interface */

#ifndef ALJEFRA_SCHED_H
#define ALJEFRA_SCHED_H

#include <stdint.h>

/* Task states */
typedef enum {
    TASK_READY   = 0,
    TASK_RUNNING = 1,
    TASK_BLOCKED = 2,
    TASK_DEAD    = 3,
} task_state_t;

/* Task descriptor */
typedef struct task {
    uint32_t     id;
    task_state_t state;
    uint32_t     core_affinity;   /* Core to run on, 0xFFFFFFFF = any */
    uint64_t     stack_base;      /* Stack allocation base */
    uint64_t     stack_size;      /* Stack size in bytes */
    uint64_t     rsp;             /* Saved stack pointer */
    uint64_t     entry;           /* Entry point */
    void        *arg;             /* Argument to entry */
    struct task *next;            /* Linked list for ready queue */
} task_t;

/* Maximum tasks */
#define SCHED_MAX_TASKS  64

/* Initialize the scheduler */
void sched_init(void);

/* Create a new task. Returns task ID or -1 on failure. */
int32_t sched_create(void (*entry)(void *), void *arg, uint64_t stack_size);

/* Yield current CPU time slice */
void sched_yield(void);

/* Block the current task (e.g., waiting for I/O) */
void sched_block(void);

/* Unblock a task by ID */
void sched_unblock(uint32_t task_id);

/* Get current task ID */
uint32_t sched_current_task(void);

/* Run the scheduler (called from timer interrupt) */
void sched_tick(void);

#endif /* ALJEFRA_SCHED_H */
