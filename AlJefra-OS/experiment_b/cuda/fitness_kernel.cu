/*
 * CUDA Fitness Prediction Kernel
 * Evaluates binary genome fitness on GPU — millions of candidates per second.
 *
 * Each CUDA thread processes one genome:
 * 1. Walk through bytes, identify instruction boundaries
 * 2. Look up latency for each instruction
 * 3. Sum total latency → inverse = fitness
 *
 * This is a simplified x86 decoder optimized for GPU parallelism.
 */

#ifdef HAVE_CUDA

#include <cuda_runtime.h>
#include <stdio.h>
#include "cuda_bridge.h"

/* Simplified instruction latency lookup on GPU.
 * Only handles the most common opcodes — enough for fitness ranking. */
__device__ float gpu_instruction_latency(unsigned char opcode, int is_2byte) {
    if (is_2byte) {
        /* 0F xx opcodes */
        if (opcode >= 0x80 && opcode <= 0x8F) return 1.0f;  /* Jcc rel32 */
        if (opcode >= 0x40 && opcode <= 0x4F) return 1.0f;  /* CMOVcc */
        if (opcode >= 0x90 && opcode <= 0x9F) return 1.0f;  /* SETcc */
        if (opcode == 0x1F) return 0.0f;                     /* Multi-byte NOP */
        if (opcode == 0xAF) return 3.0f;                     /* IMUL */
        if (opcode == 0xB6 || opcode == 0xBE) return 1.0f;  /* MOVZX/MOVSX */
        if (opcode == 0x31) return 20.0f;                    /* RDTSC */
        return 2.0f;  /* Default 2-byte */
    }

    /* 1-byte opcodes */
    if (opcode <= 0x3F) return 1.0f;         /* ALU (ADD/OR/ADC/SBB/AND/SUB/XOR/CMP) */
    if (opcode >= 0x40 && opcode <= 0x4F) return 0.0f;  /* REX prefix */
    if (opcode >= 0x50 && opcode <= 0x5F) return 3.0f;  /* PUSH/POP */
    if (opcode >= 0x70 && opcode <= 0x7F) return 1.0f;  /* Jcc short */
    if (opcode == 0x90) return 0.0f;                     /* NOP */
    if (opcode >= 0x88 && opcode <= 0x8B) return 1.0f;  /* MOV */
    if (opcode == 0x8D) return 1.0f;                     /* LEA */
    if (opcode >= 0xB0 && opcode <= 0xBF) return 1.0f;  /* MOV imm */
    if (opcode == 0xC3) return 5.0f;                     /* RET */
    if (opcode == 0xE8) return 3.0f;                     /* CALL */
    if (opcode == 0xE9 || opcode == 0xEB) return 1.0f;  /* JMP */
    if (opcode == 0xF4) return 100.0f;                   /* HLT */
    if (opcode == 0xCF) return 30.0f;                    /* IRETQ */
    if (opcode >= 0xE4 && opcode <= 0xEF) return 15.0f; /* I/O */
    return 2.0f;  /* Default */
}

/* Simplified instruction length decoder for GPU.
 * Returns number of bytes consumed. */
__device__ int gpu_instruction_length(const unsigned char *code, int max_len) {
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

    /* 2-byte escape */
    if (opcode == 0x0F) {
        if (pos >= max_len) return pos;
        opcode = code[pos++];

        /* Most 0F xx have ModR/M */
        if (pos < max_len) {
            unsigned char modrm = code[pos++];
            unsigned char mod = (modrm >> 6) & 3;
            unsigned char rm = modrm & 7;

            if (mod != 3) {
                if (rm == 4 && pos < max_len) pos++;  /* SIB */
                if (mod == 0 && rm == 5) pos += 4;
                else if (mod == 1) pos += 1;
                else if (mod == 2) pos += 4;
            }

            /* Some 0F opcodes have imm8 */
            if ((opcode >= 0x70 && opcode <= 0x73) ||
                opcode == 0xA4 || opcode == 0xAC ||
                opcode == 0xBA || opcode == 0xC2 ||
                opcode == 0xC4 || opcode == 0xC5 || opcode == 0xC6) {
                pos += 1;
            }
            /* Jcc rel32 */
            if (opcode >= 0x80 && opcode <= 0x8F) {
                pos += 4;
                pos -= 1;  /* Already counted modrm as the first rel byte */
                /* Actually, Jcc rel32 has no ModR/M — redo */
            }
        }

        /* Jcc rel32 (no ModR/M) */
        if (opcode >= 0x80 && opcode <= 0x8F) {
            return (pos - 1) + 4;  /* opcode byte + 4-byte rel */
        }

        return pos < max_len ? pos : max_len;
    }

    /* 1-byte opcodes */
    /* Immediate-only */
    if (opcode >= 0x70 && opcode <= 0x7F) return pos + 1;  /* Jcc rel8 */
    if (opcode == 0xEB) return pos + 1;  /* JMP rel8 */
    if (opcode == 0xE8 || opcode == 0xE9) return pos + 4;  /* CALL/JMP rel32 */
    if (opcode >= 0xB0 && opcode <= 0xB7) return pos + 1;  /* MOV r8, imm8 */
    if (opcode >= 0xB8 && opcode <= 0xBF) return pos + 4;  /* MOV r32, imm32 */
    if (opcode == 0x68) return pos + 4;  /* PUSH imm32 */
    if (opcode == 0x6A) return pos + 1;  /* PUSH imm8 */

    /* No operands */
    if (opcode >= 0x50 && opcode <= 0x5F) return pos;  /* PUSH/POP */
    if (opcode == 0x90 || opcode == 0xC3 || opcode == 0xCF ||
        opcode == 0xF4 || opcode == 0xC9 ||
        opcode >= 0xF8) return pos;

    /* ModR/M opcodes */
    if (pos < max_len) {
        unsigned char modrm = code[pos++];
        unsigned char mod = (modrm >> 6) & 3;
        unsigned char rm = modrm & 7;

        if (mod != 3) {
            if (rm == 4 && pos < max_len) pos++;  /* SIB */
            if (mod == 0 && rm == 5) pos += 4;
            else if (mod == 1) pos += 1;
            else if (mod == 2) pos += 4;
        }

        /* Immediate */
        if (opcode == 0x80 || opcode == 0x83 ||
            opcode == 0xC0 || opcode == 0xC1 ||
            opcode == 0xC6) pos += 1;
        if (opcode == 0x81 || opcode == 0xC7) pos += 4;
        if (opcode == 0x69) pos += 4;
        if (opcode == 0x6B) pos += 1;
    }

    return pos < max_len ? pos : max_len;
}

/* Main fitness prediction kernel.
 * One thread per genome. */
__global__ void fitness_kernel(const unsigned char *genomes,
                               const int *genome_offsets,
                               const int *genome_sizes,
                               float *fitness_out,
                               int count) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= count) return;

    const unsigned char *genome = genomes + genome_offsets[idx];
    int size = genome_sizes[idx];

    float total_latency = 0.0f;
    int num_instructions = 0;
    int nop_count = 0;
    int offset = 0;

    while (offset < size && num_instructions < 8192) {
        int len = gpu_instruction_length(genome + offset, size - offset);
        if (len <= 0) { offset++; continue; }

        /* Get opcode (skip prefixes) */
        int opc_pos = offset;
        while (opc_pos < offset + len) {
            unsigned char b = genome[opc_pos];
            if (b == 0xF0 || b == 0xF2 || b == 0xF3 ||
                b == 0x66 || b == 0x67 ||
                (b >= 0x40 && b <= 0x4F) ||
                b == 0x26 || b == 0x2E || b == 0x36 || b == 0x3E ||
                b == 0x64 || b == 0x65) {
                opc_pos++;
            } else {
                break;
            }
        }

        if (opc_pos < offset + len) {
            unsigned char opcode = genome[opc_pos];
            int is_2byte = 0;
            if (opcode == 0x0F && opc_pos + 1 < offset + len) {
                is_2byte = 1;
                opcode = genome[opc_pos + 1];
            }

            float lat = gpu_instruction_latency(opcode, is_2byte);
            total_latency += lat;

            if (opcode == 0x90 || (is_2byte && opcode == 0x1F)) {
                nop_count++;
            }
        }

        num_instructions++;
        offset += len;
    }

    /* Fitness: inverse of total latency, with bonuses/penalties */
    float fitness = 1000000.0f / (total_latency + 1.0f);
    float size_bonus = 1.0f + (10000.0f - num_instructions) / 10000.0f * 0.1f;
    if (size_bonus < 0.5f) size_bonus = 0.5f;
    float nop_penalty = 1.0f - nop_count * 0.001f;
    if (nop_penalty < 0.5f) nop_penalty = 0.5f;
    fitness *= size_bonus * nop_penalty;

    fitness_out[idx] = fitness;
}

/* ── Host API ─────────────────────────────────────────────────────── */

static int cuda_initialized = 0;
static char device_name[256] = "Unknown";

int cuda_init(void) {
    int device_count;
    cudaError_t err = cudaGetDeviceCount(&device_count);
    if (err != cudaSuccess || device_count == 0) {
        fprintf(stderr, "CUDA: No GPU devices found\n");
        return -1;
    }

    cudaDeviceProp prop;
    cudaGetDeviceProperties(&prop, 0);
    snprintf(device_name, sizeof(device_name), "%s", prop.name);
    printf("CUDA: Using %s (%d SMs, %zu MB VRAM)\n",
           prop.name, prop.multiProcessorCount,
           prop.totalGlobalMem / (1024 * 1024));

    cuda_initialized = 1;
    return 0;
}

int cuda_fitness_predict_batch(const uint8_t **genomes,
                               const uint32_t *sizes,
                               int count,
                               uint64_t base_addr,
                               double *fitness_out) {
    (void)base_addr;
    if (!cuda_initialized) return -1;

    /* Calculate total genome bytes */
    size_t total_bytes = 0;
    for (int i = 0; i < count; i++) total_bytes += sizes[i];

    /* Allocate device memory */
    unsigned char *d_genomes;
    int *d_offsets, *d_sizes;
    float *d_fitness;

    cudaMalloc(&d_genomes, total_bytes);
    cudaMalloc(&d_offsets, count * sizeof(int));
    cudaMalloc(&d_sizes, count * sizeof(int));
    cudaMalloc(&d_fitness, count * sizeof(float));

    /* Copy genomes to device */
    int *h_offsets = (int *)malloc(count * sizeof(int));
    int *h_sizes = (int *)malloc(count * sizeof(int));
    unsigned char *h_concat = (unsigned char *)malloc(total_bytes);

    size_t pos = 0;
    for (int i = 0; i < count; i++) {
        h_offsets[i] = (int)pos;
        h_sizes[i] = (int)sizes[i];
        memcpy(h_concat + pos, genomes[i], sizes[i]);
        pos += sizes[i];
    }

    cudaMemcpy(d_genomes, h_concat, total_bytes, cudaMemcpyHostToDevice);
    cudaMemcpy(d_offsets, h_offsets, count * sizeof(int), cudaMemcpyHostToDevice);
    cudaMemcpy(d_sizes, h_sizes, count * sizeof(int), cudaMemcpyHostToDevice);

    /* Launch kernel */
    int threads = 256;
    int blocks = (count + threads - 1) / threads;
    fitness_kernel<<<blocks, threads>>>(d_genomes, d_offsets, d_sizes,
                                        d_fitness, count);

    cudaError_t launch_err = cudaGetLastError();
    if (launch_err != cudaSuccess) {
        fprintf(stderr, "CUDA fitness kernel launch failed: %s\n",
                cudaGetErrorString(launch_err));
        cudaFree(d_genomes); cudaFree(d_offsets);
        cudaFree(d_sizes); cudaFree(d_fitness);
        free(h_offsets); free(h_sizes); free(h_concat);
        return -1;
    }

    cudaError_t sync_err = cudaDeviceSynchronize();
    if (sync_err != cudaSuccess) {
        fprintf(stderr, "CUDA fitness kernel sync failed: %s\n",
                cudaGetErrorString(sync_err));
        cudaFree(d_genomes); cudaFree(d_offsets);
        cudaFree(d_sizes); cudaFree(d_fitness);
        free(h_offsets); free(h_sizes); free(h_concat);
        return -1;
    }

    /* Copy results back */
    float *h_fitness = (float *)malloc(count * sizeof(float));
    cudaMemcpy(h_fitness, d_fitness, count * sizeof(float), cudaMemcpyDeviceToHost);

    for (int i = 0; i < count; i++) {
        fitness_out[i] = (double)h_fitness[i];
    }

    /* Cleanup */
    free(h_offsets);
    free(h_sizes);
    free(h_concat);
    free(h_fitness);
    cudaFree(d_genomes);
    cudaFree(d_offsets);
    cudaFree(d_sizes);
    cudaFree(d_fitness);

    return 0;
}

void cuda_cleanup(void) {
    if (cuda_initialized) {
        cudaDeviceReset();
        cuda_initialized = 0;
    }
}

const char *cuda_get_device_name(void) {
    return device_name;
}

#endif /* HAVE_CUDA */
