/*
 * CUDA Mutation Kernel
 * Generates millions of mutated genome copies in parallel on GPU.
 *
 * Each thread applies random safe substitutions from a pattern table.
 * Much faster than CPU mutation for large populations.
 */

#ifdef HAVE_CUDA

#include <cuda_runtime.h>
#include <stdio.h>
#include "cuda_bridge.h"

/* Safe substitution pattern (GPU-compatible) */
struct gpu_pattern {
    unsigned char from[8];
    unsigned char to[8];
    int len;
};

/* Subset of safe patterns for GPU mutation.
 * These are all same-size substitutions (no code shifting). */
__constant__ struct gpu_pattern d_patterns[] = {
    /* XOR reg,reg instead of MOV reg,0 — kept same size with NOPs */
    {{0x83, 0xF8, 0x00}, {0x85, 0xC0, 0x90}, 3},  /* cmp eax,0 → test eax,eax+nop */
    {{0x83, 0xF9, 0x00}, {0x85, 0xC9, 0x90}, 3},  /* cmp ecx,0 → test ecx,ecx+nop */
    {{0x83, 0xFA, 0x00}, {0x85, 0xD2, 0x90}, 3},  /* cmp edx,0 → test edx,edx+nop */
    {{0x83, 0xFB, 0x00}, {0x85, 0xDB, 0x90}, 3},  /* cmp ebx,0 → test ebx,ebx+nop */
    {{0x3C, 0x00, 0x00}, {0x84, 0xC0, 0x90}, 2},  /* cmp al,0 → test al,al (padded) */
    {{0x29, 0xC0, 0x00}, {0x31, 0xC0, 0x90}, 2},  /* sub eax,eax → xor eax,eax */
    {{0xD1, 0xE0, 0x00}, {0x01, 0xC0, 0x90}, 2},  /* shl eax,1 → add eax,eax */
    {{0xD1, 0xE1, 0x00}, {0x01, 0xC9, 0x90}, 2},  /* shl ecx,1 → add ecx,ecx */
    {{0x83, 0xC0, 0x01}, {0xFF, 0xC0, 0x90}, 3},  /* add eax,1 → inc eax+nop */
    {{0x83, 0xE8, 0x01}, {0xFF, 0xC8, 0x90}, 3},  /* sub eax,1 → dec eax+nop */
    {{0x90, 0x90, 0x00}, {0x66, 0x90, 0x00}, 2},  /* 2x nop → 2-byte nop */
};

#define NUM_GPU_PATTERNS 11

/* Immutable region bounds (jump table) */
#define IMM_START 0x10   /* Offset from kernel base */
#define IMM_END   0xA8

/* Simple GPU-friendly PRNG (xorshift32) */
__device__ unsigned int gpu_rand(unsigned int *state) {
    unsigned int x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

/* Mutation kernel: one thread per genome copy */
__global__ void mutation_kernel(const unsigned char *seed,
                                unsigned char *output,
                                int genome_size,
                                int count,
                                unsigned int base_seed) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= count) return;

    /* Copy seed genome to output */
    unsigned char *my_genome = output + (size_t)idx * genome_size;
    for (int i = 0; i < genome_size; i++) {
        my_genome[i] = seed[i];
    }

    /* Initialize per-thread RNG */
    unsigned int rng = base_seed ^ (idx * 2654435761u);

    /* Apply 1-3 mutations */
    int num_mutations = 1 + (gpu_rand(&rng) % 3);

    for (int m = 0; m < num_mutations; m++) {
        /* Pick a random position */
        int pos = gpu_rand(&rng) % (genome_size - 8);

        /* Skip immutable region */
        if (pos >= IMM_START && pos < IMM_END) continue;

        /* Try each pattern */
        int pat = gpu_rand(&rng) % NUM_GPU_PATTERNS;
        struct gpu_pattern p = d_patterns[pat];

        /* Check if pattern matches at this position */
        int match = 1;
        for (int j = 0; j < p.len; j++) {
            if (my_genome[pos + j] != p.from[j]) {
                match = 0;
                break;
            }
        }

        if (match) {
            /* Apply substitution */
            for (int j = 0; j < p.len; j++) {
                my_genome[pos + j] = p.to[j];
            }
        }
    }
}

int cuda_mutate_batch(const uint8_t *seed_genome,
                      uint32_t genome_size,
                      uint8_t **mutated_out,
                      int count,
                      unsigned int seed) {
    unsigned char *d_seed, *d_output;
    size_t total = (size_t)genome_size * count;

    printf("  [GPU] Allocating %.1f MB on device for %d genomes...\n",
           (double)total / (1024*1024), count);

    cudaError_t err;
    err = cudaMalloc(&d_seed, genome_size);
    if (err != cudaSuccess) {
        fprintf(stderr, "CUDA: Failed to alloc seed: %s\n", cudaGetErrorString(err));
        return -1;
    }
    err = cudaMalloc(&d_output, total);
    if (err != cudaSuccess) {
        fprintf(stderr, "CUDA: Failed to alloc output (%.1f MB): %s\n",
                (double)total/(1024*1024), cudaGetErrorString(err));
        cudaFree(d_seed);
        return -1;
    }

    cudaMemcpy(d_seed, seed_genome, genome_size, cudaMemcpyHostToDevice);

    int threads = 256;
    int blocks = (count + threads - 1) / threads;
    printf("  [GPU] Launching mutation kernel: %d blocks x %d threads\n", blocks, threads);

    mutation_kernel<<<blocks, threads>>>(d_seed, d_output, genome_size,
                                         count, seed);

    cudaError_t launch_err = cudaGetLastError();
    if (launch_err != cudaSuccess) {
        fprintf(stderr, "CUDA mutation kernel launch failed: %s\n",
                cudaGetErrorString(launch_err));
        cudaFree(d_seed); cudaFree(d_output);
        return -1;
    }

    cudaError_t sync_err = cudaDeviceSynchronize();
    if (sync_err != cudaSuccess) {
        fprintf(stderr, "CUDA mutation kernel sync failed: %s\n",
                cudaGetErrorString(sync_err));
        cudaFree(d_seed); cudaFree(d_output);
        return -1;
    }

    /* Copy results back in one large transfer */
    unsigned char *h_output = (unsigned char *)malloc(total);
    if (!h_output) {
        fprintf(stderr, "Host: Failed to alloc %.1f MB for results\n",
                (double)total/(1024*1024));
        cudaFree(d_seed); cudaFree(d_output);
        return -1;
    }
    cudaMemcpy(h_output, d_output, total, cudaMemcpyDeviceToHost);

    /* Write directly into caller's pre-allocated buffers (no per-genome malloc) */
    for (int i = 0; i < count; i++) {
        if (mutated_out[i]) {
            memcpy(mutated_out[i], h_output + (size_t)i * genome_size, genome_size);
        }
    }

    free(h_output);
    cudaFree(d_seed);
    cudaFree(d_output);

    return 0;
}

#endif /* HAVE_CUDA */
