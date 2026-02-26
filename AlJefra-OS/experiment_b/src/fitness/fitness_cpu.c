/*
 * CPU-based Fitness Prediction
 * Predicts binary fitness using an instruction-level latency model.
 * Parallelized with OpenMP for batch evaluation.
 */
#include "fitness_cpu.h"
#include "latency_tables.h"
#include "../decoder/x86_decode.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

#ifdef _OPENMP
#include <omp.h>
#endif

/* Score a decoded instruction stream using latency model.
 * Lower total latency = higher fitness.
 * Also rewards:
 * - Fewer instructions (smaller code)
 * - More macro-fusible pairs (CMP/TEST + Jcc adjacent)
 * - Better alignment of branch targets
 * - Fewer dependency chains
 */
static double score_instructions(const instruction_t *instrs, int count) {
    if (count <= 0) return 0.0;

    double total_latency = 0.0;
    double total_throughput = 0.0;
    int nop_count = 0;
    int fusion_pairs = 0;
    int aligned_targets = 0;
    int branch_targets = 0;

    for (int i = 0; i < count; i++) {
        const instruction_t *in = &instrs[i];

        /* Get latency from tables */
        uint16_t lat = 0;
        uint16_t thr = 5;

        /* Find the actual opcode byte (skip prefixes) */
        int opc_pos = 0;
        uint8_t has_rex_w = 0;
        for (int j = 0; j < in->length; j++) {
            uint8_t b = in->bytes[j];
            if (b >= 0x40 && b <= 0x4F) {
                has_rex_w = (b & 0x08) ? 1 : 0;
                opc_pos = j + 1;
                continue;
            }
            if (b == 0xF0 || b == 0xF2 || b == 0xF3 ||
                b == 0x66 || b == 0x67 ||
                b == 0x26 || b == 0x2E || b == 0x36 ||
                b == 0x3E || b == 0x64 || b == 0x65) {
                opc_pos = j + 1;
                continue;
            }
            break;
        }

        if (opc_pos < in->length) {
            uint8_t opcode = in->bytes[opc_pos];
            if (opcode == 0x0F && opc_pos + 1 < in->length) {
                lat = latency_get_2byte(in->bytes[opc_pos + 1]);
                thr = throughput_get_2byte(in->bytes[opc_pos + 1]);
            } else {
                lat = latency_get_1byte(opcode, has_rex_w);
                thr = throughput_get_1byte(opcode, has_rex_w);
            }
        }

        total_latency += lat;
        total_throughput += thr;

        /* Count NOPs */
        if (in->is_nop) nop_count++;

        /* Check for macro-fusion opportunities */
        if (i + 1 < count) {
            const instruction_t *next = &instrs[i + 1];
            if (next->is_branch && !in->is_branch && !in->is_nop) {
                /* CMP/TEST followed by Jcc = fusible */
                if (opc_pos < in->length) {
                    uint8_t op = in->bytes[opc_pos];
                    if (op == 0x84 || op == 0x85 ||  /* TEST */
                        (op >= 0x38 && op <= 0x3D) || /* CMP */
                        op == 0x80 || op == 0x81 || op == 0x83) {
                        fusion_pairs++;
                    }
                }
            }
        }

        /* Check branch target alignment */
        if (in->label[0] != '\0') {
            branch_targets++;
            if ((in->address & 0xF) == 0) {
                aligned_targets++;
            }
        }
    }

    /* Fitness formula:
     * Base: inverse of total throughput (lower cycles = higher fitness)
     * Bonuses:
     *   - Instruction count reduction (fewer = better, less decode pressure)
     *   - Macro-fusion pairs (each saves ~1 cycle)
     *   - Aligned targets (fewer cache line splits)
     * Penalties:
     *   - Excessive NOPs (wasted decode bandwidth)
     */
    double base_fitness = 1000000.0 / (total_throughput + 1.0);
    double size_bonus = 1.0 + (10000.0 - count) / 10000.0 * 0.1;
    double fusion_bonus = 1.0 + fusion_pairs * 0.005;
    double align_bonus = 1.0;
    if (branch_targets > 0) {
        align_bonus = 1.0 + (double)aligned_targets / branch_targets * 0.05;
    }
    double nop_penalty = 1.0 - nop_count * 0.001;
    if (nop_penalty < 0.5) nop_penalty = 0.5;

    return base_fitness * size_bonus * fusion_bonus * align_bonus * nop_penalty;
}

double fitness_predict(const uint8_t *genome, uint32_t size,
                       uint64_t base_addr) {
    instruction_t instrs[MAX_INSTRUCTIONS];
    int n = x86_decode_all(genome, size, base_addr, instrs, MAX_INSTRUCTIONS);
    if (n <= 0) return 0.0;
    return score_instructions(instrs, n);
}

void fitness_predict_batch(const uint8_t **genomes,
                           const uint32_t *sizes,
                           int count,
                           uint64_t base_addr,
                           double *fitness_out) {
    #pragma omp parallel for schedule(dynamic, 16)
    for (int i = 0; i < count; i++) {
        fitness_out[i] = fitness_predict(genomes[i], sizes[i], base_addr);
    }
}

double fitness_composite_score(const benchmark_result_t *bench) {
    if (!bench->valid || !bench->boot_success) return 0.0;

    /* Weighted composite:
     * - Kernel latency: 30% (lower is better, invert)
     * - Memory bandwidth: 25% (higher is better)
     * - SMP scaling: 20% (higher is better)
     * - Net latency: 10% (lower is better, invert)
     * - NVS latency: 10% (lower is better, invert)
     * - GPU latency: 5% (lower is better, invert)
     */
    double score = 0.0;

    if (bench->kernel_latency_us > 0)
        score += 0.30 * (1000.0 / bench->kernel_latency_us);
    if (bench->mem_bandwidth_mbs > 0)
        score += 0.25 * (bench->mem_bandwidth_mbs / 100.0);
    if (bench->smp_scaling_pct > 0)
        score += 0.20 * (bench->smp_scaling_pct / 100.0);
    if (bench->net_latency_us > 0)
        score += 0.10 * (1000.0 / bench->net_latency_us);
    if (bench->nvs_latency_us > 0)
        score += 0.10 * (1000.0 / bench->nvs_latency_us);
    if (bench->gpu_latency_us > 0)
        score += 0.05 * (1000.0 / bench->gpu_latency_us);

    return score;
}

double fitness_improvement_pct(const benchmark_result_t *baseline,
                               const benchmark_result_t *candidate) {
    double base_score = fitness_composite_score(baseline);
    double cand_score = fitness_composite_score(candidate);

    if (base_score <= 0.0) return 0.0;
    return ((cand_score - base_score) / base_score) * 100.0;
}
