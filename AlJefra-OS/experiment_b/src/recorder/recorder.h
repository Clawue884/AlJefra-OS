/*
 * Breakthrough Recorder
 * Records evolution breakthroughs — git branches + JSONL log
 */
#ifndef RECORDER_H
#define RECORDER_H

#include "../config.h"

/* Record a breakthrough: create git branch, write to log, save binary */
int recorder_save_breakthrough(const breakthrough_t *bt,
                               const uint8_t *kernel_bin,
                               uint32_t kernel_size,
                               const char *work_dir);

/* Append a generation result to the evolution log */
int recorder_log_generation(uint32_t generation, component_id_t component,
                            double best_fitness, double avg_fitness,
                            int population_size, int breakthroughs,
                            const char *work_dir);

/* Get timestamp string */
void recorder_timestamp(char *buf, int buf_size);

#endif /* RECORDER_H */
