/*
 * GA Selection — Tournament Selection
 */
#include "population.h"
#include <stdlib.h>

int tournament_select(const population_t *pop, int tournament_size) {
    int best_idx = rand() % pop->size;
    double best_fit = pop->individuals[best_idx].predicted_fitness;

    for (int i = 1; i < tournament_size; i++) {
        int idx = rand() % pop->size;
        double fit = pop->individuals[idx].predicted_fitness;
        if (fit > best_fit) {
            best_fit = fit;
            best_idx = idx;
        }
    }

    return best_idx;
}
