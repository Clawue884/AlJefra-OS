// =============================================================================
// AlJefra OS AI -- GPU Evolution Engine
// Copyright (C) 2026 -- see LICENSE.TXT
//
// GPU Engine C API -- Reusable GPU interface for all OS components
// This header is the SINGLE POINT OF ENTRY for any component needing GPU access.
// Never write direct GPU code -- always use this engine.
//
// Version 1.0
// =============================================================================

#ifndef _GPU_ENGINE_H
#define _GPU_ENGINE_H

#include <stdint.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int64_t i64;

// ============================================================================
// GPU Status
// ============================================================================

// Status bits returned by gpu_status()
#define GPU_STATUS_PRESENT		(1 << 0)
#define GPU_STATUS_INITIALIZED		(1 << 1)
#define GPU_STATUS_COMPUTE_READY	(1 << 2)
#define GPU_STATUS_FIFO_ACTIVE		(1 << 3)

// Check if GPU is available and ready
u64 gpu_status(void);

// Returns 1 if GPU is present and compute-ready, 0 otherwise
static inline int gpu_available(void) {
	u64 s = gpu_status();
	return (s & (GPU_STATUS_PRESENT | GPU_STATUS_COMPUTE_READY)) ==
	       (GPU_STATUS_PRESENT | GPU_STATUS_COMPUTE_READY);
}


// ============================================================================
// GPU Memory Management
// ============================================================================

// Allocate VRAM (returned as offset, not pointer)
// Returns 0xFFFFFFFFFFFFFFFF on failure
u64 gpu_mem_alloc(u64 size_bytes);

// Free VRAM
void gpu_mem_free(u64 vram_offset, u64 size_bytes);

// DMA: Copy system memory -> VRAM. Returns fence ID.
u64 gpu_mem_copy_to(void *src, u64 vram_dst, u64 size_bytes);

// DMA: Copy VRAM -> system memory. Returns fence ID.
u64 gpu_mem_copy_from(u64 vram_src, void *dst, u64 size_bytes);

// Get VRAM info
typedef struct {
	u64 total_bytes;
	u64 free_bytes;
} gpu_vram_info_t;

void gpu_vram_info(gpu_vram_info_t *info);


// ============================================================================
// GPU Compute Dispatch
// ============================================================================

// Compute parameters structure (matches kernel ABI)
typedef struct {
	u64 shader_addr;	// Shader/kernel address in VRAM
	u32 grid_x;		// Grid dimensions (number of blocks)
	u32 grid_y;
	u32 grid_z;
	u32 block_x;		// Block dimensions (threads per block)
	u32 block_y;
	u32 block_z;
	u64 input_buffer;	// Input buffer VRAM offset
	u64 output_buffer;	// Output buffer VRAM offset
	u64 input_size;		// Input size in bytes
	u64 output_size;	// Output size in bytes
} gpu_compute_params_t;

// Dispatch compute workload. Returns fence ID (0xFFFFFFFF on failure).
u64 gpu_compute(gpu_compute_params_t *params);


// ============================================================================
// GPU Synchronization
// ============================================================================

// Wait for a GPU operation to complete (by fence ID)
void gpu_fence_wait(u64 fence_id);


// ============================================================================
// GPU Direct Access (advanced)
// ============================================================================

// Read/write GPU MMIO registers directly
u32 gpu_mmio_read(u32 reg_offset);
void gpu_mmio_write(u32 reg_offset, u32 value);


// ============================================================================
// GPU Benchmark
// ============================================================================

// Run GPU command latency benchmark
u64 gpu_benchmark(void);


// ============================================================================
// Evolution Engine GPU Helpers
// These functions provide higher-level abstractions used by the evolution
// framework. They combine multiple low-level GPU calls into useful patterns.
// ============================================================================

// Upload data to VRAM, run compute, download results -- all in one call
// Returns 0 on success, -1 on failure
int gpu_compute_sync(
	u64 shader_vram,		// Pre-uploaded shader in VRAM
	void *input, u64 input_size,	// Input data in system memory
	void *output, u64 output_size,	// Output buffer in system memory
	u32 grid_x, u32 grid_y, u32 grid_z,
	u32 block_x, u32 block_y, u32 block_z
);

// Matrix multiply on GPU (used by AI/ML): C = A * B
// All matrices in row-major, f32 elements
// Returns 0 on success
int gpu_matmul_f32(
	float *A, u32 rows_a, u32 cols_a,
	float *B, u32 rows_b, u32 cols_b,
	float *C  // Output: rows_a x cols_b
);

// Vector dot product on GPU
// Returns 0 on success, result stored in *result
int gpu_dot_f32(float *a, float *b, u32 len, float *result);

// Element-wise operations on GPU
int gpu_relu_f32(float *data, u32 len);		// ReLU activation
int gpu_sigmoid_f32(float *data, u32 len);	// Sigmoid activation
int gpu_softmax_f32(float *data, u32 len);	// Softmax

// Tensor operations
typedef struct {
	float *data;		// Pointer to data in system memory
	u32 dims[4];		// Dimensions [batch, channels, height, width]
	u32 ndims;		// Number of active dimensions (1-4)
	u64 vram_offset;	// Cached VRAM location (0 = not uploaded)
} gpu_tensor_t;

// Upload tensor to VRAM (caches vram_offset in tensor struct)
int gpu_tensor_upload(gpu_tensor_t *t);

// Download tensor from VRAM
int gpu_tensor_download(gpu_tensor_t *t);

// Free tensor's VRAM allocation
void gpu_tensor_free(gpu_tensor_t *t);


// ============================================================================
// Evolution-Specific GPU Functions
// ============================================================================

// Genetic algorithm helpers
typedef struct {
	u8 *genome;		// Binary genome data
	u64 genome_size;	// Size in bytes
	float fitness;		// Fitness score
} gpu_genome_t;

// Evaluate fitness of a population on GPU (parallel)
int gpu_evolve_evaluate(gpu_genome_t *population, u32 pop_size, u64 fitness_shader);

// Crossover and mutation on GPU
int gpu_evolve_crossover(gpu_genome_t *parents, u32 parent_count,
			  gpu_genome_t *offspring, u32 offspring_count,
			  float mutation_rate, u64 rng_seed);

// Select best genomes from population
int gpu_evolve_select(gpu_genome_t *population, u32 pop_size,
		       gpu_genome_t *selected, u32 select_count);


#endif // _GPU_ENGINE_H


// =============================================================================
// EOF
