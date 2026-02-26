/*
 * CUDA Bridge — Interface between C host code and CUDA kernels
 * Only compiled when HAVE_CUDA is defined
 */
#ifndef CUDA_BRIDGE_H
#define CUDA_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef HAVE_CUDA

#include <stdint.h>

/* Initialize CUDA device */
int cuda_init(void);

/* GPU fitness prediction: evaluate N genomes in parallel.
 * Each genome is decoded into instructions, and latency is summed.
 * Returns fitness scores in fitness_out[]. */
int cuda_fitness_predict_batch(const uint8_t **genomes,
                               const uint32_t *sizes,
                               int count,
                               uint64_t base_addr,
                               double *fitness_out);

/* GPU mutation: generate N mutated copies of a genome in parallel.
 * Applies random safe substitutions from the pattern table.
 * Results written to mutated_out[] (caller allocates). */
int cuda_mutate_batch(const uint8_t *seed_genome,
                      uint32_t genome_size,
                      uint8_t **mutated_out,
                      int count,
                      unsigned int seed);

/* GPU-side evolution: each of population_size threads runs
 * iterations_per_thread rounds of mutation+evaluation.
 * Returns best genome and its fitness. */
/* mut_start/mut_end = byte offsets for targeted function evolution */
int cuda_gpu_evolve(const uint8_t *seed_genome,
                    uint32_t genome_size,
                    uint8_t *best_genome_out,
                    float *best_fitness_out,
                    int population_size,
                    int iterations_per_thread,
                    unsigned int seed,
                    int mut_start,
                    int mut_end);

/* Security analysis results from GPU evolution */
struct gpu_security_stats {
    float worst_fitness;       /* Lowest fitness seen (max damage from mutation) */
    float best_fitness;        /* Best fitness achieved */
    float fragility;           /* % fitness drop from worst mutation */
    float avg_mutations;       /* Average mutations per thread that applied */
    int   most_damaged_thread; /* Thread that found worst fitness */
};

/* Full GPU evolution with security analysis.
 * sec_out may be NULL to skip security tracking. */
int cuda_gpu_evolve_full(const uint8_t *seed_genome,
                          uint32_t genome_size,
                          uint8_t *best_genome_out,
                          float *best_fitness_out,
                          int population_size,
                          int iterations_per_thread,
                          unsigned int seed,
                          int mut_start,
                          int mut_end,
                          struct gpu_security_stats *sec_out);

/* Free CUDA resources */
void cuda_cleanup(void);

/* Get GPU info string */
const char *cuda_get_device_name(void);

#else /* !HAVE_CUDA */

/* Stubs when CUDA not available */
static inline int cuda_init(void) { return -1; }
static inline void cuda_cleanup(void) {}
static inline const char *cuda_get_device_name(void) { return "N/A"; }

#endif /* HAVE_CUDA */

#ifdef __cplusplus
}
#endif

#endif /* CUDA_BRIDGE_H */
