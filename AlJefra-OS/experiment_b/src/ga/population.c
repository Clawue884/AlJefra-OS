/*
 * GA Population Management
 */
#include "population.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

int population_init(population_t *pop, int capacity,
                    const uint8_t *seed_genome, uint32_t genome_size) {
    pop->individuals = calloc(capacity, sizeof(individual_t));
    if (!pop->individuals) return -1;

    pop->size = capacity;
    pop->capacity = capacity;
    pop->generation = 0;
    pop->best_fitness = 0.0;
    pop->best_index = 0;
    pop->genome_size = genome_size;
    pop->baseline_fitness = 0.0;

    for (int i = 0; i < capacity; i++) {
        pop->individuals[i].genome = malloc(genome_size);
        if (!pop->individuals[i].genome) {
            /* Cleanup on failure */
            for (int j = 0; j < i; j++) free(pop->individuals[j].genome);
            free(pop->individuals);
            return -1;
        }
        memcpy(pop->individuals[i].genome, seed_genome, genome_size);
        pop->individuals[i].genome_size = genome_size;
        pop->individuals[i].predicted_fitness = 0.0;
        pop->individuals[i].actual_fitness = 0.0;
        pop->individuals[i].generation = 0;
        pop->individuals[i].mutations_applied = 0;
        pop->individuals[i].validated = 0;
        pop->individuals[i].is_breakthrough = 0;
    }

    return 0;
}

void population_free(population_t *pop) {
    if (pop->individuals) {
        for (int i = 0; i < pop->capacity; i++) {
            free(pop->individuals[i].genome);
        }
        free(pop->individuals);
        pop->individuals = NULL;
    }
    pop->size = 0;
    pop->capacity = 0;
}

/* Comparison function for qsort (descending fitness) */
static int cmp_fitness_desc(const void *a, const void *b) {
    const individual_t *ia = (const individual_t *)a;
    const individual_t *ib = (const individual_t *)b;
    if (ib->predicted_fitness > ia->predicted_fitness) return 1;
    if (ib->predicted_fitness < ia->predicted_fitness) return -1;
    return 0;
}

void population_sort(population_t *pop) {
    qsort(pop->individuals, pop->size, sizeof(individual_t), cmp_fitness_desc);

    /* Update best */
    pop->best_fitness = pop->individuals[0].predicted_fitness;
    pop->best_index = 0;
}

individual_t *population_best(population_t *pop) {
    return &pop->individuals[pop->best_index];
}

void population_top_n(population_t *pop, int n, int *out_indices) {
    /* Assumes population is sorted */
    for (int i = 0; i < n && i < pop->size; i++) {
        out_indices[i] = i;
    }
}

void population_replace_worst(population_t *pop,
                              individual_t *offspring, int count) {
    /* Replace the worst (last) individuals */
    for (int i = 0; i < count && i < pop->size; i++) {
        int idx = pop->size - 1 - i;
        /* Free old genome, swap in new */
        free(pop->individuals[idx].genome);
        pop->individuals[idx] = offspring[i];
        /* Don't free offspring genome — we transferred ownership */
    }
}

void population_print_stats(const population_t *pop) {
    double sum = 0, min_f = 1e18, max_f = 0;
    int validated = 0, breakthroughs = 0;

    for (int i = 0; i < pop->size; i++) {
        double f = pop->individuals[i].predicted_fitness;
        sum += f;
        if (f < min_f) min_f = f;
        if (f > max_f) max_f = f;
        if (pop->individuals[i].validated) validated++;
        if (pop->individuals[i].is_breakthrough) breakthroughs++;
    }

    printf("  Gen %u: best=%.2f avg=%.2f min=%.2f | "
           "validated=%d breakthroughs=%d\n",
           pop->generation, max_f, sum / pop->size, min_f,
           validated, breakthroughs);
}
