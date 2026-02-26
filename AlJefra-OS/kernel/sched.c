/* SPDX-License-Identifier: MIT */
/* AlJefra OS — Simple Round-Robin Scheduler
 *
 * Minimal cooperative scheduler for the exokernel.
 * Tasks are lightweight and share the same address space.
 */

#include "sched.h"
#include "../hal/hal.h"

/* Task pool */
static task_t g_tasks[SCHED_MAX_TASKS];
static uint32_t g_task_count;
static uint32_t g_current_task;
static hal_spinlock_t g_sched_lock = HAL_SPINLOCK_INIT;

/* Simple stack allocation base (grows down from high memory) */
#define TASK_STACK_BASE  0x800000   /* 8 MB region for task stacks */

void sched_init(void)
{
    for (uint32_t i = 0; i < SCHED_MAX_TASKS; i++) {
        g_tasks[i].id = i;
        g_tasks[i].state = TASK_DEAD;
        g_tasks[i].next = NULL;
    }

    /* Task 0 = kernel main (already running) */
    g_tasks[0].state = TASK_RUNNING;
    g_tasks[0].core_affinity = 0;
    g_task_count = 1;
    g_current_task = 0;

    hal_console_puts("[sched] Initialized with 1 task (kernel)\n");
}

int32_t sched_create(void (*entry)(void *), void *arg, uint64_t stack_size)
{
    hal_spin_lock(&g_sched_lock);

    /* Find a free slot */
    int32_t slot = -1;
    for (uint32_t i = 1; i < SCHED_MAX_TASKS; i++) {
        if (g_tasks[i].state == TASK_DEAD) {
            slot = (int32_t)i;
            break;
        }
    }

    if (slot < 0) {
        hal_spin_unlock(&g_sched_lock);
        return -1;
    }

    if (stack_size == 0)
        stack_size = 65536; /* 64 KB default */

    task_t *t = &g_tasks[slot];
    t->state = TASK_READY;
    t->core_affinity = 0xFFFFFFFF;
    t->stack_base = TASK_STACK_BASE + (uint64_t)slot * stack_size;
    t->stack_size = stack_size;
    t->rsp = t->stack_base + stack_size - 8; /* Top of stack */
    t->entry = (uint64_t)entry;
    t->arg = arg;
    t->next = NULL;
    g_task_count++;

    hal_console_printf("[sched] Created task %d\n", slot);

    hal_spin_unlock(&g_sched_lock);
    return slot;
}

void sched_yield(void)
{
    /* Cooperative yield: find next READY task */
    hal_spin_lock(&g_sched_lock);

    if (g_task_count <= 1) {
        hal_spin_unlock(&g_sched_lock);
        return;
    }

    g_tasks[g_current_task].state = TASK_READY;

    uint32_t next = g_current_task;
    for (uint32_t i = 0; i < SCHED_MAX_TASKS; i++) {
        next = (next + 1) % SCHED_MAX_TASKS;
        if (g_tasks[next].state == TASK_READY) {
            g_tasks[next].state = TASK_RUNNING;
            g_current_task = next;
            break;
        }
    }

    hal_spin_unlock(&g_sched_lock);
}

void sched_block(void)
{
    hal_spin_lock(&g_sched_lock);
    g_tasks[g_current_task].state = TASK_BLOCKED;
    hal_spin_unlock(&g_sched_lock);
    sched_yield();
}

void sched_unblock(uint32_t task_id)
{
    if (task_id >= SCHED_MAX_TASKS) return;
    hal_spin_lock(&g_sched_lock);
    if (g_tasks[task_id].state == TASK_BLOCKED)
        g_tasks[task_id].state = TASK_READY;
    hal_spin_unlock(&g_sched_lock);
}

uint32_t sched_current_task(void)
{
    return g_current_task;
}

void sched_tick(void)
{
    /* Called from timer interrupt — preemptive scheduling */
    /* For now, just yield cooperatively */
    sched_yield();
}
