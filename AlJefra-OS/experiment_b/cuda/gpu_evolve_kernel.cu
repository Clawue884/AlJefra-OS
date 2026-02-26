/*
 * GPU Evolution Kernel — Full mutation+fitness loop runs on GPU
 * Each thread manages one genome: mutate → evaluate → keep/discard → repeat
 * 65K threads × 1000 iterations = 65 million fitness evaluations per launch
 */

#ifdef HAVE_CUDA

#include <cuda_runtime.h>
#include <stdio.h>
#include "cuda_bridge.h"

/* ── Device-side helpers ──────────────────────────────────────────── */

/* xorshift32 PRNG */
__device__ unsigned int gpu_rng(unsigned int *state) {
    unsigned int x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

/* Safe substitution patterns (same-size, no code shifting) */
struct evo_pattern {
    unsigned char from[4];
    unsigned char to[4];
    int len;
};

__constant__ struct evo_pattern evo_patterns[] = {
    /* cmp reg,0 → test reg,reg + nop */
    {{0x83, 0xF8, 0x00, 0}, {0x85, 0xC0, 0x90, 0}, 3},
    {{0x83, 0xF9, 0x00, 0}, {0x85, 0xC9, 0x90, 0}, 3},
    {{0x83, 0xFA, 0x00, 0}, {0x85, 0xD2, 0x90, 0}, 3},
    {{0x83, 0xFB, 0x00, 0}, {0x85, 0xDB, 0x90, 0}, 3},
    /* sub reg,reg → xor reg,reg */
    {{0x29, 0xC0, 0, 0}, {0x31, 0xC0, 0, 0}, 2},
    {{0x29, 0xC9, 0, 0}, {0x31, 0xC9, 0, 0}, 2},
    {{0x29, 0xD2, 0, 0}, {0x31, 0xD2, 0, 0}, 2},
    /* shl reg,1 → add reg,reg */
    {{0xD1, 0xE0, 0, 0}, {0x01, 0xC0, 0, 0}, 2},
    {{0xD1, 0xE1, 0, 0}, {0x01, 0xC9, 0, 0}, 2},
    {{0xD1, 0xE2, 0, 0}, {0x01, 0xD2, 0, 0}, 2},
    /* add reg,1 → inc reg + nop */
    {{0x83, 0xC0, 0x01, 0}, {0xFF, 0xC0, 0x90, 0}, 3},
    {{0x83, 0xC1, 0x01, 0}, {0xFF, 0xC1, 0x90, 0}, 3},
    /* sub reg,1 → dec reg + nop */
    {{0x83, 0xE8, 0x01, 0}, {0xFF, 0xC8, 0x90, 0}, 3},
    {{0x83, 0xE9, 0x01, 0}, {0xFF, 0xC9, 0x90, 0}, 3},
    /* 2x nop → 2-byte nop */
    {{0x90, 0x90, 0, 0}, {0x66, 0x90, 0, 0}, 2},
    /* REX.W xor eax,eax → xor eax,eax (shorter) — padded */
    {{0x48, 0x31, 0xC0, 0}, {0x31, 0xC0, 0x90, 0}, 3},
    {{0x48, 0x31, 0xC9, 0}, {0x31, 0xC9, 0x90, 0}, 3},
    /* mov reg,0 → xor reg,reg + 2x nop */
    {{0xB8, 0x00, 0x00, 0x00}, {0x31, 0xC0, 0x90, 0x90}, 4},
    {{0xB9, 0x00, 0x00, 0x00}, {0x31, 0xC9, 0x90, 0x90}, 4},
};

#define NUM_EVO_PATTERNS 19

/* Immutable jump table region */
#define JT_START 0x10
#define JT_END   0xA8

/* Simplified instruction length decoder */
__device__ int evo_instr_len(const unsigned char *code, int max_len) {
    if (max_len <= 0) return 0;
    int pos = 0;

    /* Skip prefixes */
    while (pos < max_len) {
        unsigned char b = code[pos];
        if (b == 0xF0 || b == 0xF2 || b == 0xF3 ||
            b == 0x66 || b == 0x67 ||
            b == 0x26 || b == 0x2E || b == 0x36 || b == 0x3E ||
            b == 0x64 || b == 0x65 ||
            (b >= 0x40 && b <= 0x4F)) {
            pos++;
            continue;
        }
        break;
    }
    if (pos >= max_len) return max_len;

    unsigned char opcode = code[pos++];

    if (opcode == 0x0F) {
        if (pos >= max_len) return pos;
        unsigned char op2 = code[pos++];
        if (op2 >= 0x80 && op2 <= 0x8F) return pos + 4; /* Jcc rel32 */
        if (pos < max_len) {
            unsigned char modrm = code[pos++];
            unsigned char mod = (modrm >> 6) & 3;
            unsigned char rm = modrm & 7;
            if (mod != 3) {
                if (rm == 4 && pos < max_len) pos++;
                if (mod == 0 && rm == 5) pos += 4;
                else if (mod == 1) pos += 1;
                else if (mod == 2) pos += 4;
            }
        }
        return pos < max_len ? pos : max_len;
    }

    if (opcode >= 0x70 && opcode <= 0x7F) return pos + 1;
    if (opcode == 0xEB) return pos + 1;
    if (opcode == 0xE8 || opcode == 0xE9) return pos + 4;
    if (opcode >= 0xB0 && opcode <= 0xB7) return pos + 1;
    if (opcode >= 0xB8 && opcode <= 0xBF) return pos + 4;
    if (opcode == 0x68) return pos + 4;
    if (opcode == 0x6A) return pos + 1;
    if (opcode >= 0x50 && opcode <= 0x5F) return pos;
    if (opcode == 0x90 || opcode == 0xC3 || opcode == 0xCF ||
        opcode == 0xF4 || opcode == 0xC9 || opcode >= 0xF8)
        return pos;

    if (pos < max_len) {
        unsigned char modrm = code[pos++];
        unsigned char mod = (modrm >> 6) & 3;
        unsigned char rm = modrm & 7;
        if (mod != 3) {
            if (rm == 4 && pos < max_len) pos++;
            if (mod == 0 && rm == 5) pos += 4;
            else if (mod == 1) pos += 1;
            else if (mod == 2) pos += 4;
        }
        if (opcode == 0x80 || opcode == 0x83 || opcode == 0xC0 ||
            opcode == 0xC1 || opcode == 0xC6) pos += 1;
        if (opcode == 0x81 || opcode == 0xC7 || opcode == 0x69) pos += 4;
        if (opcode == 0x6B) pos += 1;
    }
    return pos < max_len ? pos : max_len;
}

/* Instruction latency (simplified) */
__device__ float evo_latency(unsigned char opcode, int is_2byte) {
    if (is_2byte) {
        if (opcode >= 0x80 && opcode <= 0x8F) return 1.0f;
        if (opcode >= 0x40 && opcode <= 0x4F) return 1.0f;
        if (opcode == 0x1F) return 0.0f;
        if (opcode == 0xAF) return 3.0f;
        if (opcode == 0xB6 || opcode == 0xBE) return 1.0f;
        return 2.0f;
    }
    if (opcode <= 0x3F) return 1.0f;
    if (opcode >= 0x40 && opcode <= 0x4F) return 0.0f;
    if (opcode >= 0x50 && opcode <= 0x5F) return 3.0f;
    if (opcode >= 0x70 && opcode <= 0x7F) return 1.0f;
    if (opcode == 0x90) return 0.0f;
    if (opcode >= 0x88 && opcode <= 0x8B) return 1.0f;
    if (opcode == 0x8D) return 1.0f;
    if (opcode >= 0xB0 && opcode <= 0xBF) return 1.0f;
    if (opcode == 0xC3) return 5.0f;
    if (opcode == 0xE8) return 3.0f;
    if (opcode == 0xE9 || opcode == 0xEB) return 1.0f;
    if (opcode == 0xF4) return 100.0f;
    if (opcode == 0xCF) return 30.0f;
    return 2.0f;
}

/* Evaluate fitness of a genome */
__device__ float evo_evaluate(const unsigned char *genome, int size) {
    float total_latency = 0.0f;
    int num_instr = 0;
    int nop_count = 0;
    int offset = 0;

    while (offset < size && num_instr < 8192) {
        int len = evo_instr_len(genome + offset, size - offset);
        if (len <= 0) { offset++; continue; }

        /* Find opcode (skip prefixes) */
        int opc_pos = offset;
        while (opc_pos < offset + len) {
            unsigned char b = genome[opc_pos];
            if (b == 0xF0 || b == 0xF2 || b == 0xF3 ||
                b == 0x66 || b == 0x67 ||
                (b >= 0x40 && b <= 0x4F) ||
                b == 0x26 || b == 0x2E || b == 0x36 || b == 0x3E ||
                b == 0x64 || b == 0x65) {
                opc_pos++;
            } else break;
        }

        if (opc_pos < offset + len) {
            unsigned char op = genome[opc_pos];
            int is2 = 0;
            if (op == 0x0F && opc_pos + 1 < offset + len) {
                is2 = 1; op = genome[opc_pos + 1];
            }
            total_latency += evo_latency(op, is2);
            if (op == 0x90 || (is2 && op == 0x1F)) nop_count++;
        }

        num_instr++;
        offset += len;
    }

    float fitness = 1000000.0f / (total_latency + 1.0f);
    float size_bonus = 1.0f + (10000.0f - num_instr) / 10000.0f * 0.1f;
    if (size_bonus < 0.5f) size_bonus = 0.5f;
    float nop_pen = 1.0f - nop_count * 0.001f;
    if (nop_pen < 0.5f) nop_pen = 0.5f;
    return fitness * size_bonus * nop_pen;
}

/* ── Main evolution kernel ────────────────────────────────────────── */

/* mut_start/mut_end = byte offsets within the genome where mutations are allowed.
 * This restricts evolution to a single function's code region. */
/* worst_fitness_out: if non-NULL, each thread also tracks the WORST fitness
 * seen across all mutations (measures function fragility/vulnerability).
 * mutations_tried_out: if non-NULL, counts how many mutations actually applied. */
__global__ void gpu_evolve_kernel(const unsigned char *seed,
                                   unsigned char *output,
                                   float *fitness_out,
                                   float *worst_fitness_out,
                                   int *mutations_tried_out,
                                   int genome_size,
                                   int count,
                                   int iterations,
                                   unsigned int base_seed,
                                   int mut_start,
                                   int mut_end) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= count) return;

    unsigned char *my_genome = output + (size_t)idx * genome_size;

    /* Copy seed genome */
    for (int i = 0; i < genome_size; i++) {
        my_genome[i] = seed[i];
    }

    /* Init RNG */
    unsigned int rng = base_seed ^ (idx * 2654435761u) ^ (idx * 1103515245u);
    gpu_rng(&rng); gpu_rng(&rng); /* Warm up */

    float best_fitness = evo_evaluate(my_genome, genome_size);
    float worst_fitness = best_fitness;
    int mutations_applied = 0;
    int mut_range = mut_end - mut_start;
    if (mut_range < 4) mut_range = 4;

    /* Evolution loop: try mutations only within [mut_start, mut_end) */
    for (int iter = 0; iter < iterations; iter++) {
        int pos = mut_start + (gpu_rng(&rng) % mut_range);
        if (pos + 4 > genome_size) continue;

        /* Skip immutable jump table */
        if (pos >= JT_START && pos < JT_END) continue;

        /* Mutation strategy selection:
         *   0 = pattern substitution (exact match, rare)
         *   1 = ModRM register field mutation (common, every ModRM instr)
         *   2 = NOP insertion/removal (swap byte with 0x90)
         *   3 = REX prefix toggle (add/remove REX.W for 64-bit ops)
         */
        int strategy = gpu_rng(&rng) % 4;
        unsigned char saved[8];
        int mutated = 0;
        int mut_len = 1;

        if (strategy == 0) {
            /* Pattern substitution (original approach) */
            int pat = gpu_rng(&rng) % NUM_EVO_PATTERNS;
            struct evo_pattern p = evo_patterns[pat];
            if (pos + p.len <= mut_end) {
                int match = 1;
                for (int j = 0; j < p.len; j++) {
                    if (my_genome[pos + j] != p.from[j]) { match = 0; break; }
                }
                if (match) {
                    mut_len = p.len;
                    for (int j = 0; j < p.len; j++) saved[j] = my_genome[pos + j];
                    for (int j = 0; j < p.len; j++) my_genome[pos + j] = p.to[j];
                    mutated = 1;
                }
            }
        } else if (strategy == 1) {
            /* ModRM register field mutation: find an instruction with ModRM,
             * then flip register bits in the reg or rm field.
             * This changes which register is used but preserves the operation
             * type and instruction length. */
            unsigned char byte = my_genome[pos];
            /* Look for opcodes that take a ModRM byte */
            int has_modrm = 0;
            if ((byte >= 0x00 && byte <= 0x03) ||   /* ADD r/m, r */
                (byte >= 0x08 && byte <= 0x0B) ||   /* OR */
                (byte >= 0x20 && byte <= 0x23) ||   /* AND */
                (byte >= 0x28 && byte <= 0x2B) ||   /* SUB */
                (byte >= 0x30 && byte <= 0x33) ||   /* XOR */
                (byte >= 0x38 && byte <= 0x3B) ||   /* CMP */
                (byte >= 0x80 && byte <= 0x8B) ||   /* immediate arith, MOV */
                byte == 0x8D ||                      /* LEA */
                (byte >= 0xC0 && byte <= 0xC1) ||   /* shift/rotate */
                byte == 0xD3 || byte == 0xF7 ||     /* shift/mul/div */
                byte == 0xFF)                        /* inc/dec/call/jmp */
                has_modrm = 1;

            if (has_modrm && pos + 1 < mut_end) {
                unsigned char modrm = my_genome[pos + 1];
                unsigned char mod_field = (modrm >> 6) & 3;
                /* Only mutate reg field (bits 5:3) when mod=11 (register mode)
                 * to avoid creating memory references */
                if (mod_field == 3) {
                    saved[0] = my_genome[pos + 1];
                    mut_len = 1;
                    /* Flip a random bit in reg(5:3) or rm(2:0) */
                    int which = gpu_rng(&rng) % 2;
                    if (which == 0) {
                        /* Mutate reg field (bits 5:3) */
                        int new_reg = gpu_rng(&rng) % 8;
                        my_genome[pos + 1] = (modrm & 0xC7) | (new_reg << 3);
                    } else {
                        /* Mutate rm field (bits 2:0) */
                        int new_rm = gpu_rng(&rng) % 8;
                        my_genome[pos + 1] = (modrm & 0xF8) | new_rm;
                    }
                    if (my_genome[pos + 1] != saved[0]) mutated = 1;
                    /* Adjust pos for the save/restore to work on modrm byte */
                    pos = pos + 1;
                }
            }
        } else if (strategy == 2) {
            /* NOP swap: replace a byte with NOP or replace NOP with
             * adjacent byte's value. Only in register-mode instructions. */
            saved[0] = my_genome[pos];
            if (my_genome[pos] == 0x90 && pos + 1 < mut_end) {
                /* NOP → try duplicating adjacent instruction's opcode
                 * (effectively a no-op that changes decode pattern) */
                /* Just leave it, NOPs are already penalized by fitness */
            } else if (my_genome[pos] != 0x90) {
                /* Try inserting a NOP by swapping with a nearby NOP */
                for (int scan = pos + 1; scan < pos + 8 && scan < mut_end; scan++) {
                    if (my_genome[scan] == 0x90) {
                        saved[0] = my_genome[pos];
                        saved[1] = my_genome[scan];
                        my_genome[scan] = my_genome[pos];
                        my_genome[pos] = 0x90;
                        mut_len = 2; /* need to restore 2 positions */
                        mutated = 1;
                        break;
                    }
                }
            }
        } else {
            /* REX prefix toggle: add/remove REX.W (0x48) prefix.
             * For 64-bit code, some instructions work with or without REX. */
            if (my_genome[pos] == 0x48 && pos + 1 < mut_end) {
                unsigned char next = my_genome[pos + 1];
                /* Remove REX.W from common safe opcodes */
                if ((next >= 0x31 && next <= 0x33) ||  /* XOR */
                    (next >= 0x29 && next <= 0x2B) ||  /* SUB */
                    (next >= 0x01 && next <= 0x03) ||  /* ADD */
                    (next >= 0x09 && next <= 0x0B) ||  /* OR */
                    (next >= 0x21 && next <= 0x23) ||  /* AND */
                    next == 0x85 || next == 0x89 ||    /* TEST, MOV */
                    next == 0x8B || next == 0x8D)      /* MOV, LEA */
                {
                    saved[0] = 0x48;
                    my_genome[pos] = 0x90; /* Replace REX.W with NOP */
                    mutated = 1;
                }
            }
        }

        if (mutated) {
            mutations_applied++;
            float new_fitness = evo_evaluate(my_genome, genome_size);

            /* Track worst fitness seen (measures fragility) */
            if (new_fitness < worst_fitness) {
                worst_fitness = new_fitness;
            }

            if (new_fitness > best_fitness) {
                best_fitness = new_fitness;
            } else {
                /* Revert */
                if (strategy == 2 && mut_len == 2) {
                    /* NOP swap: restore both positions */
                    my_genome[pos] = saved[0];
                    /* Find the swapped position */
                    for (int scan = pos + 1; scan < pos + 8 && scan < mut_end; scan++) {
                        if (my_genome[scan] == saved[0]) {
                            my_genome[scan] = saved[1];
                            break;
                        }
                    }
                } else if (strategy == 1) {
                    /* ModRM: pos was adjusted to point at modrm byte */
                    my_genome[pos] = saved[0];
                } else if (strategy == 0) {
                    for (int j = 0; j < mut_len; j++)
                        my_genome[pos + j] = saved[j];
                } else {
                    my_genome[pos] = saved[0];
                }
            }
        }
    }

    fitness_out[idx] = best_fitness;
    if (worst_fitness_out) worst_fitness_out[idx] = worst_fitness;
    if (mutations_tried_out) mutations_tried_out[idx] = mutations_applied;
}

/* ── Host API ─────────────────────────────────────────────────────── */

/* Host-side fitness approximation (can't call __device__ from host) */
static float evo_evaluate_host(const uint8_t *genome, int size) {
    float total_latency = 0.0f;
    int num_instr = 0, nop_count = 0, offset = 0;
    while (offset < size && num_instr < 8192) {
        unsigned char op = genome[offset];
        if (op == 0x90) { nop_count++; }
        else if (op == 0xC3 || op == 0xCF) total_latency += 5.0f;
        else if (op >= 0x50 && op <= 0x5F) total_latency += 3.0f;
        else total_latency += 1.5f;
        num_instr++;
        offset++;
    }
    float fitness = 1000000.0f / (total_latency + 1.0f);
    float size_bonus = 1.0f + (10000.0f - num_instr) / 10000.0f * 0.1f;
    if (size_bonus < 0.5f) size_bonus = 0.5f;
    float nop_pen = 1.0f - nop_count * 0.001f;
    if (nop_pen < 0.5f) nop_pen = 0.5f;
    return fitness * size_bonus * nop_pen;
}

extern "C" int cuda_gpu_evolve(const uint8_t *seed_genome,
                                uint32_t genome_size,
                                uint8_t *best_genome_out,
                                float *best_fitness_out,
                                int population_size,
                                int iterations_per_thread,
                                unsigned int seed,
                                int mut_start,
                                int mut_end) {
    /* Forward to full version with no security output */
    return cuda_gpu_evolve_full(seed_genome, genome_size, best_genome_out,
                                 best_fitness_out, population_size,
                                 iterations_per_thread, seed,
                                 mut_start, mut_end, NULL);
}

extern "C" int cuda_gpu_evolve_full(const uint8_t *seed_genome,
                                     uint32_t genome_size,
                                     uint8_t *best_genome_out,
                                     float *best_fitness_out,
                                     int population_size,
                                     int iterations_per_thread,
                                     unsigned int seed,
                                     int mut_start,
                                     int mut_end,
                                     struct gpu_security_stats *sec_out) {
    unsigned char *d_seed, *d_output;
    float *d_fitness, *d_worst;
    int *d_mutations;
    size_t total = (size_t)genome_size * population_size;

    printf("    [GPU] %d threads x %d iters = %.1fM evals | range [0x%x-0x%x]\n",
           population_size, iterations_per_thread,
           (double)population_size * iterations_per_thread / 1e6,
           mut_start, mut_end);

    cudaError_t err;
    err = cudaMalloc(&d_seed, genome_size);
    if (err != cudaSuccess) {
        fprintf(stderr, "CUDA evolve: seed alloc failed: %s\n", cudaGetErrorString(err));
        return -1;
    }
    err = cudaMalloc(&d_output, total);
    if (err != cudaSuccess) {
        fprintf(stderr, "CUDA evolve: output alloc failed (%.1f MB): %s\n",
                (double)total/(1024*1024), cudaGetErrorString(err));
        cudaFree(d_seed);
        return -1;
    }
    err = cudaMalloc(&d_fitness, population_size * sizeof(float));
    if (err != cudaSuccess) {
        fprintf(stderr, "CUDA evolve: fitness alloc failed: %s\n", cudaGetErrorString(err));
        cudaFree(d_seed); cudaFree(d_output);
        return -1;
    }

    /* Allocate security tracking arrays */
    err = cudaMalloc(&d_worst, population_size * sizeof(float));
    if (err != cudaSuccess) { cudaFree(d_seed); cudaFree(d_output); cudaFree(d_fitness); return -1; }
    err = cudaMalloc(&d_mutations, population_size * sizeof(int));
    if (err != cudaSuccess) { cudaFree(d_seed); cudaFree(d_output); cudaFree(d_fitness); cudaFree(d_worst); return -1; }

    cudaMemcpy(d_seed, seed_genome, genome_size, cudaMemcpyHostToDevice);

    int threads = 256;
    int blocks = (population_size + threads - 1) / threads;
    printf("    [GPU] Launching %d blocks x %d threads...\n", blocks, threads);

    gpu_evolve_kernel<<<blocks, threads>>>(d_seed, d_output, d_fitness,
                                            d_worst, d_mutations,
                                            genome_size, population_size,
                                            iterations_per_thread, seed,
                                            mut_start, mut_end);

    err = cudaGetLastError();
    if (err != cudaSuccess) {
        fprintf(stderr, "CUDA evolve kernel launch failed: %s\n", cudaGetErrorString(err));
        cudaFree(d_seed); cudaFree(d_output); cudaFree(d_fitness);
        cudaFree(d_worst); cudaFree(d_mutations);
        return -1;
    }

    err = cudaDeviceSynchronize();
    if (err != cudaSuccess) {
        fprintf(stderr, "CUDA evolve kernel sync failed: %s\n", cudaGetErrorString(err));
        cudaFree(d_seed); cudaFree(d_output); cudaFree(d_fitness);
        cudaFree(d_worst); cudaFree(d_mutations);
        return -1;
    }

    /* Copy fitness back and find best */
    float *h_fitness = (float *)malloc(population_size * sizeof(float));
    float *h_worst = (float *)malloc(population_size * sizeof(float));
    int *h_mutations = (int *)malloc(population_size * sizeof(int));

    cudaMemcpy(h_fitness, d_fitness, population_size * sizeof(float), cudaMemcpyDeviceToHost);
    cudaMemcpy(h_worst, d_worst, population_size * sizeof(float), cudaMemcpyDeviceToHost);
    cudaMemcpy(h_mutations, d_mutations, population_size * sizeof(int), cudaMemcpyDeviceToHost);

    int best_idx = 0;
    float best_f = h_fitness[0];
    float global_worst = h_worst[0];
    int worst_idx = 0;
    long long total_mutations = 0;

    for (int i = 0; i < population_size; i++) {
        if (h_fitness[i] > best_f) {
            best_f = h_fitness[i];
            best_idx = i;
        }
        if (h_worst[i] < global_worst) {
            global_worst = h_worst[i];
            worst_idx = i;
        }
        total_mutations += h_mutations[i];
    }

    *best_fitness_out = best_f;

    /* Copy best genome back */
    cudaMemcpy(best_genome_out, d_output + (size_t)best_idx * genome_size,
               genome_size, cudaMemcpyDeviceToHost);

    printf("    [GPU] Best: thread %d fitness=%.2f\n", best_idx, best_f);

    /* Fill security stats if requested */
    if (sec_out) {
        float baseline = evo_evaluate_host(seed_genome, genome_size);
        sec_out->best_fitness = best_f;
        sec_out->worst_fitness = global_worst;
        sec_out->fragility = (baseline > 0) ?
            (baseline - global_worst) / baseline * 100.0f : 0;
        sec_out->avg_mutations = (float)total_mutations / population_size;
        sec_out->most_damaged_thread = worst_idx;
    }

    free(h_fitness);
    free(h_worst);
    free(h_mutations);
    cudaFree(d_seed);
    cudaFree(d_output);
    cudaFree(d_fitness);
    cudaFree(d_worst);
    cudaFree(d_mutations);

    return 0;
}

#endif /* HAVE_CUDA */
