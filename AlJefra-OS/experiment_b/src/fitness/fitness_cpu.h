/*
 * CPU-based Fitness Prediction
 * Uses instruction latency model to predict performance.
 * OpenMP parallelized for batch evaluation.
 */
#ifndef FITNESS_CPU_H
#define FITNESS_CPU_H

#include "../config.h"

/* Predict fitness for a single genome (higher = better).
 * Uses instruction latency model to estimate total cycles. */
double fitness_predict(const uint8_t *genome, uint32_t size,
                       uint64_t base_addr);

/* Predict fitness for a batch of genomes (OpenMP parallel).
 * Results written to fitness_out[]. */
void fitness_predict_batch(const uint8_t **genomes,
                           const uint32_t *sizes,
                           int count,
                           uint64_t base_addr,
                           double *fitness_out);

/* Calculate a composite score from benchmark results.
 * Used to compare QEMU-validated results. */
double fitness_composite_score(const benchmark_result_t *bench);

/* Compare two benchmark results.
 * Returns improvement percentage (positive = better). */
double fitness_improvement_pct(const benchmark_result_t *baseline,
                               const benchmark_result_t *candidate);

#endif /* FITNESS_CPU_H */
