/*
 * Breakthrough Recorder
 */
#include "recorder.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Component names — single definition for all modules */
const char *component_names[COMP_COUNT] = {
    "kernel", "memory", "smp", "network", "storage",
    "gpu_driver", "bus", "interrupts", "timer", "io",
    "syscalls", "vram_alloc", "cmd_queue", "dma", "scheduler"
};

void recorder_timestamp(char *buf, int buf_size) {
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    strftime(buf, buf_size, "%Y-%m-%dT%H:%M:%S", tm);
}

int recorder_save_breakthrough(const breakthrough_t *bt,
                               const uint8_t *kernel_bin,
                               uint32_t kernel_size,
                               const char *work_dir) {
    char cmd[1024];
    char timestamp[32];
    recorder_timestamp(timestamp, sizeof(timestamp));

    printf("\n*** BREAKTHROUGH! Component=%s Gen=%u Improvement=+%.1f%% ***\n",
           component_names[bt->component], bt->generation, bt->improvement_pct);

    /* 1. Save the mutated kernel binary */
    char bin_path[512];
    snprintf(bin_path, sizeof(bin_path), "results/%s_gen%04u.bin",
             component_names[bt->component], bt->generation);
    (void)work_dir;

    FILE *f = fopen(bin_path, "wb");
    if (f) {
        fwrite(kernel_bin, 1, kernel_size, f);
        fclose(f);
        printf("  Saved binary: %s\n", bin_path);
    }

    /* 2. Create git branch (if in a git repo) */
    char branch[256];
    snprintf(branch, sizeof(branch), BREAKTHROUGH_BRANCH_FMT,
             component_names[bt->component], bt->generation);

    snprintf(cmd, sizeof(cmd),
        "cd .. && "
        "git rev-parse --git-dir > /dev/null 2>&1 && "
        "git checkout -b '%s' 2>/dev/null && "
        "cp 'experiment_b/%s' sys/kernel.sys 2>/dev/null && "
        "git add sys/kernel.sys 2>/dev/null && "
        "git commit -m 'Binary evolution: %s +%.1f%% (gen %u)' 2>/dev/null && "
        "git checkout - 2>/dev/null",
        branch, bin_path,
        component_names[bt->component], bt->improvement_pct, bt->generation);

    int git_ok = system(cmd);
    if (git_ok == 0) {
        printf("  Created branch: %s\n", branch);
    }

    /* 3. Append to JSONL log */
    char log_path[512];
    snprintf(log_path, sizeof(log_path), "results/breakthroughs.jsonl");

    f = fopen(log_path, "a");
    if (f) {
        fprintf(f, "{\"timestamp\":\"%s\",\"experiment\":\"binary_evolution\","
                "\"component\":\"%s\",\"generation\":%u,"
                "\"improvement_pct\":%.2f,"
                "\"baseline_score\":%.4f,\"evolved_score\":%.4f,"
                "\"branch\":\"%s\"}\n",
                timestamp, component_names[bt->component], bt->generation,
                bt->improvement_pct, bt->baseline_score, bt->evolved_score,
                branch);
        fclose(f);
    }

    /* 4. Also append to shared evolution log */
    char shared_log[512];
    snprintf(shared_log, sizeof(shared_log),
             "../evolution/logs/evolution_log.jsonl");

    f = fopen(shared_log, "a");
    if (f) {
        fprintf(f, "{\"timestamp\":\"%s\",\"experiment\":\"B_binary_evolution\","
                "\"component\":\"%s\",\"generation\":%u,"
                "\"improvement_pct\":%.2f,"
                "\"method\":\"gpu_binary_mutation\"}\n",
                timestamp, component_names[bt->component], bt->generation,
                bt->improvement_pct);
        fclose(f);
    }

    return 0;
}

int recorder_log_generation(uint32_t generation, component_id_t component,
                            double best_fitness, double avg_fitness,
                            int population_size, int breakthroughs,
                            const char *work_dir) {
    char log_path[512];
    (void)work_dir;
    snprintf(log_path, sizeof(log_path), "results/generations.jsonl");

    char timestamp[32];
    recorder_timestamp(timestamp, sizeof(timestamp));

    FILE *f = fopen(log_path, "a");
    if (!f) return -1;

    fprintf(f, "{\"timestamp\":\"%s\",\"generation\":%u,"
            "\"component\":\"%s\","
            "\"best_fitness\":%.4f,\"avg_fitness\":%.4f,"
            "\"population\":%d,\"breakthroughs\":%d}\n",
            timestamp, generation, component_names[component],
            best_fitness, avg_fitness, population_size, breakthroughs);

    fclose(f);
    return 0;
}
