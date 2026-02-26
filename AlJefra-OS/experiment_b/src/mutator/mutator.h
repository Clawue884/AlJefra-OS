/*
 * Binary Mutation Engine
 * Applies safe mutations to x86-64 binary code
 */
#ifndef MUTATOR_H
#define MUTATOR_H

#include "../config.h"

/* Initialize the mutation engine (seed RNG, load patterns) */
void mutator_init(unsigned int seed);

/* Apply a single random mutation to a genome.
 * Returns 1 if mutation was applied, 0 if no safe mutation found. */
int mutator_apply(uint8_t *genome, uint32_t genome_size,
                  const instruction_t *instructions, int num_instructions,
                  const component_region_t *region,
                  mutation_type_t preferred_type);

/* Apply N random mutations to a genome.
 * Returns number of mutations actually applied. */
int mutator_apply_n(uint8_t *genome, uint32_t genome_size,
                    const instruction_t *instructions, int num_instructions,
                    const component_region_t *region,
                    int n_mutations);

/* Validate that a mutation preserves safety invariants.
 * Returns 1 if safe, 0 if invariant violated. */
int mutator_validate(const uint8_t *original, const uint8_t *mutated,
                     uint32_t size,
                     const instruction_t *orig_instructions,
                     int num_orig_instructions);

/* Check stack balance: every push has matching pop before ret */
int mutator_check_stack_balance(const instruction_t *instructions,
                                int num_instructions);

/* Check branch target validity after mutation */
int mutator_check_branch_targets(const uint8_t *code, uint32_t size,
                                 uint64_t base_addr);

#endif /* MUTATOR_H */
