#ifndef AI_CORE_H
#define AI_CORE_H

#include <stdint.h>
#include <stddef.h>

#define AI_MAX_PROCESSES 256
#define AI_SYSCALL_HISTORY 1024

typedef struct {
    uint64_t syscall_count;
    uint64_t cpu_cycles;
    uint64_t memory_usage;
    uint64_t io_operations;
} ai_process_profile_t;

typedef struct {
    uint64_t timestamp;
    uint32_t syscall_id;
    uint32_t pid;
} ai_syscall_event_t;

void ai_core_init(void);
void ai_core_tick(void);

void ai_record_syscall(uint32_t pid, uint32_t syscall_id);
void ai_update_process(uint32_t pid, uint64_t cpu, uint64_t mem, uint64_t io);

int ai_predict_priority(uint32_t pid);
size_t ai_predict_memory(uint32_t pid);

#endif
