/*
 * GA Population Management
 * Manages population of binary kernel candidates
 */
#ifndef POPULATION_H
#define POPULATION_H

#include "../config.h"

/* Population */
typedef struct {
    individual_t *individuals;
    int           size;
    int           capacity;
    uint32_t      generation;
    double        best_fitness;
    int           best_index;
    uint32_t      genome_size;      /* All individuals share same genome size */
    double        baseline_fitness; /* Fitness of unmodified kernel */
} population_t;

/* Create a new population. All individuals start as copies of the seed genome. */
int population_init(population_t *pop, int capacity,
                    const uint8_t *seed_genome, uint32_t genome_size);

/* Free population memory */
void population_free(population_t *pop);

/* Sort population by predicted fitness (descending) */
void population_sort(population_t *pop);

/* Get the best individual */
individual_t *population_best(population_t *pop);

/* Get the N best individuals (indices written to out_indices) */
void population_top_n(population_t *pop, int n, int *out_indices);

/* Replace the worst individuals with new offspring */
void population_replace_worst(population_t *pop,
                              individual_t *offspring, int count);

/* Print population statistics */
void population_print_stats(const population_t *pop);

/* Tournament selection — returns index of winner */
int tournament_select(const population_t *pop, int tournament_size);

/* Crossover two parents at instruction boundary to produce offspring */
int crossover(const individual_t *parent_a, const individual_t *parent_b,
              individual_t *offspring,
              const instruction_t *instructions, int num_instructions);

#endif /* POPULATION_H */
