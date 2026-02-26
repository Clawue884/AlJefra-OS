/*
 * GA Crossover — Instruction-boundary-aware crossover
 *
 * Unlike byte-level crossover, we only split at instruction boundaries
 * to avoid creating invalid instructions.
 */
#include "population.h"
#include "../decoder/x86_decode.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

int crossover(const individual_t *parent_a, const individual_t *parent_b,
              individual_t *offspring,
              const instruction_t *instructions, int num_instructions) {
    if (parent_a->genome_size != parent_b->genome_size) {
        fprintf(stderr, "Crossover: genome size mismatch\n");
        return -1;
    }

    uint32_t size = parent_a->genome_size;

    /* Allocate offspring genome */
    offspring->genome = malloc(size);
    if (!offspring->genome) return -1;
    offspring->genome_size = size;
    offspring->predicted_fitness = 0.0;
    offspring->actual_fitness = 0.0;
    offspring->validated = 0;
    offspring->is_breakthrough = 0;
    offspring->mutations_applied = 0;

    /* Find a valid crossover point at an instruction boundary */
    if (num_instructions < 4) {
        /* Too few instructions — just copy parent A */
        memcpy(offspring->genome, parent_a->genome, size);
        offspring->generation = parent_a->generation + 1;
        return 0;
    }

    /* Pick a random instruction boundary (avoid first/last 10%) */
    int margin = num_instructions / 10;
    if (margin < 2) margin = 2;
    int cross_idx = margin + (rand() % (num_instructions - 2 * margin));

    /* Get the byte offset of the crossover point */
    uint64_t cross_addr = instructions[cross_idx].address;
    uint64_t cross_offset = cross_addr - KERNEL_BASE_ADDR;

    if (cross_offset >= size) {
        /* Fallback to midpoint */
        cross_offset = size / 2;
    }

    /* Don't cross within immutable region */
    uint64_t imm_start = IMMUTABLE_START - KERNEL_BASE_ADDR;
    uint64_t imm_end = IMMUTABLE_END - KERNEL_BASE_ADDR;
    if (cross_offset >= imm_start && cross_offset <= imm_end) {
        cross_offset = imm_end + 1;
    }

    /* Build offspring: first half from A, second half from B */
    memcpy(offspring->genome, parent_a->genome, cross_offset);
    memcpy(offspring->genome + cross_offset,
           parent_b->genome + cross_offset,
           size - cross_offset);

    /* Ensure immutable region is preserved from parent A */
    if (imm_end <= size) {
        memcpy(offspring->genome + imm_start,
               parent_a->genome + imm_start,
               imm_end - imm_start);
    }

    offspring->generation = parent_a->generation + 1;
    return 0;
}
