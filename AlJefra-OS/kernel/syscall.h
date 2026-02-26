/* SPDX-License-Identifier: MIT */
/* AlJefra OS — Syscall Dispatch Interface */

#ifndef ALJEFRA_SYSCALL_H
#define ALJEFRA_SYSCALL_H

#include <stdint.h>

/* Syscall numbers (architecture-independent) */
#define SYS_INPUT          0
#define SYS_OUTPUT         1
#define SYS_NET_TX         2
#define SYS_NET_RX         3
#define SYS_NVS_READ       4
#define SYS_NVS_WRITE      5
#define SYS_SYSTEM         6
#define SYS_GPU_STATUS     7
#define SYS_GPU_COMPUTE    8
#define SYS_GPU_MEM_ALLOC  9
#define SYS_GPU_MEM_FREE   10
#define SYS_SCHED_YIELD    11
#define SYS_SCHED_CREATE   12
#define SYS_DRIVER_LOAD    13
#define SYS_MAX            14

/* Syscall dispatch — called from arch-specific trap handler */
uint64_t syscall_dispatch(uint64_t num, uint64_t a1, uint64_t a2,
                          uint64_t a3, uint64_t a4);

/* Main syscall processing loop */
void syscall_loop(void);

/* Initialize syscall table */
void syscall_init(void);

#endif /* ALJEFRA_SYSCALL_H */
