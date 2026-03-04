#include "ai_core.h"

static int adaptive_weight_cpu = 1;
static int adaptive_weight_io  = 2;
static int adaptive_weight_mem = 1;

void ai_adjust_weights(void) {

    uint64_t total_cpu = 0;
    uint64_t total_io  = 0;

    for (int i = 0; i < AI_MAX_PROCESSES; i++) {
        total_cpu += profiles[i].cpu_cycles;
        total_io  += profiles[i].io_operations;
    }

    if (total_io > total_cpu)
        adaptive_weight_io++;
    else
        adaptive_weight_cpu++;

    if (adaptive_weight_io > 10) adaptive_weight_io = 10;
    if (adaptive_weight_cpu > 10) adaptive_weight_cpu = 10;
}
