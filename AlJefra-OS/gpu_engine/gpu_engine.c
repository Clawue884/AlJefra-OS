// =============================================================================
// AlJefra OS AI -- GPU Evolution Engine
// Copyright (C) 2026 -- see LICENSE.TXT
//
// GPU Engine Implementation -- Reusable GPU interface for all OS components
// =============================================================================

#include "gpu_engine.h"


// ============================================================================
// Low-level syscall wrappers (match kernel ABI)
// ============================================================================

// b_system function codes for GPU (0x80-0x8F range in extended table)
#define GPU_SYS_STATUS		0x80
#define GPU_SYS_COMPUTE		0x81
#define GPU_SYS_MEM_ALLOC	0x82
#define GPU_SYS_MEM_FREE	0x83
#define GPU_SYS_MEM_COPY_TO	0x84
#define GPU_SYS_MEM_COPY_FROM	0x85
#define GPU_SYS_FENCE_WAIT	0x86
#define GPU_SYS_MMIO_READ	0x87
#define GPU_SYS_MMIO_WRITE	0x88
#define GPU_SYS_VRAM_INFO	0x89
#define GPU_SYS_BENCHMARK	0x8A

// Direct kernel call addresses (extended function table)
// These are the addresses of the GPU syscalls in the kernel
#define KERNEL_GPU_STATUS	0x00100050
#define KERNEL_GPU_COMPUTE	0x00100058
#define KERNEL_GPU_MEM_ALLOC	0x00100060
#define KERNEL_GPU_MEM_FREE	0x00100068
#define KERNEL_GPU_COPY_TO	0x00100070
#define KERNEL_GPU_COPY_FROM	0x00100078
#define KERNEL_GPU_FENCE_WAIT	0x00100080
#define KERNEL_GPU_MMIO_READ	0x00100088
#define KERNEL_GPU_MMIO_WRITE	0x00100090
#define KERNEL_GPU_VRAM_INFO	0x00100098
#define KERNEL_GPU_BENCHMARK	0x001000A0


// ============================================================================
// Core GPU Functions
// ============================================================================

u64 gpu_status(void) {
	u64 result;
	asm volatile ("call *%1" : "=a"(result) : "r"((u64)KERNEL_GPU_STATUS));
	return result;
}

u64 gpu_mem_alloc(u64 size_bytes) {
	u64 result;
	asm volatile ("call *%1" : "=a"(result) : "r"((u64)KERNEL_GPU_MEM_ALLOC), "c"(size_bytes));
	return result;
}

void gpu_mem_free(u64 vram_offset, u64 size_bytes) {
	asm volatile ("call *%0" : : "r"((u64)KERNEL_GPU_MEM_FREE), "a"(vram_offset), "c"(size_bytes));
}

u64 gpu_mem_copy_to(void *src, u64 vram_dst, u64 size_bytes) {
	u64 fence;
	asm volatile ("call *%1" : "=a"(fence) : "r"((u64)KERNEL_GPU_COPY_TO), "S"(src), "D"(vram_dst), "c"(size_bytes));
	return fence;
}

u64 gpu_mem_copy_from(u64 vram_src, void *dst, u64 size_bytes) {
	u64 fence;
	asm volatile ("call *%1" : "=a"(fence) : "r"((u64)KERNEL_GPU_COPY_FROM), "S"(vram_src), "D"(dst), "c"(size_bytes));
	return fence;
}

void gpu_vram_info(gpu_vram_info_t *info) {
	u64 total, free;
	asm volatile ("call *%2" : "=a"(total), "=d"(free) : "r"((u64)KERNEL_GPU_VRAM_INFO));
	info->total_bytes = total;
	info->free_bytes = free;
}

u64 gpu_compute(gpu_compute_params_t *params) {
	u64 fence;
	asm volatile ("call *%1" : "=a"(fence) : "r"((u64)KERNEL_GPU_COMPUTE), "a"(params));
	return fence;
}

void gpu_fence_wait(u64 fence_id) {
	asm volatile ("call *%0" : : "r"((u64)KERNEL_GPU_FENCE_WAIT), "a"(fence_id));
}

u32 gpu_mmio_read(u32 reg_offset) {
	u32 result;
	asm volatile ("call *%1" : "=a"(result) : "r"((u64)KERNEL_GPU_MMIO_READ), "c"((u64)reg_offset));
	return result;
}

void gpu_mmio_write(u32 reg_offset, u32 value) {
	asm volatile ("call *%0" : : "r"((u64)KERNEL_GPU_MMIO_WRITE), "c"((u64)reg_offset), "a"((u64)value));
}

u64 gpu_benchmark(void) {
	u64 result;
	asm volatile ("call *%1" : "=a"(result) : "r"((u64)KERNEL_GPU_BENCHMARK));
	return result;
}


// ============================================================================
// High-Level Compute Helpers
// ============================================================================

int gpu_compute_sync(
	u64 shader_vram,
	void *input, u64 input_size,
	void *output, u64 output_size,
	u32 grid_x, u32 grid_y, u32 grid_z,
	u32 block_x, u32 block_y, u32 block_z
) {
	if (!gpu_available()) return -1;

	// Allocate VRAM for input and output buffers
	u64 input_vram = gpu_mem_alloc(input_size);
	if (input_vram == 0xFFFFFFFFFFFFFFFF) return -1;

	u64 output_vram = gpu_mem_alloc(output_size);
	if (output_vram == 0xFFFFFFFFFFFFFFFF) {
		gpu_mem_free(input_vram, input_size);
		return -1;
	}

	// Upload input data to VRAM
	u64 fence = gpu_mem_copy_to(input, input_vram, input_size);
	gpu_fence_wait(fence);

	// Dispatch compute
	gpu_compute_params_t params;
	params.shader_addr = shader_vram;
	params.grid_x = grid_x;
	params.grid_y = grid_y;
	params.grid_z = grid_z;
	params.block_x = block_x;
	params.block_y = block_y;
	params.block_z = block_z;
	params.input_buffer = input_vram;
	params.output_buffer = output_vram;
	params.input_size = input_size;
	params.output_size = output_size;

	fence = gpu_compute(&params);
	if (fence == 0xFFFFFFFF) {
		gpu_mem_free(input_vram, input_size);
		gpu_mem_free(output_vram, output_size);
		return -1;
	}
	gpu_fence_wait(fence);

	// Download results from VRAM
	fence = gpu_mem_copy_from(output_vram, output, output_size);
	gpu_fence_wait(fence);

	// Free VRAM
	gpu_mem_free(input_vram, input_size);
	gpu_mem_free(output_vram, output_size);

	return 0;
}

int gpu_matmul_f32(
	float *A, u32 rows_a, u32 cols_a,
	float *B, u32 rows_b, u32 cols_b,
	float *C
) {
	if (!gpu_available()) return -1;
	if (cols_a != rows_b) return -1;

	u64 size_a = (u64)rows_a * cols_a * sizeof(float);
	u64 size_b = (u64)rows_b * cols_b * sizeof(float);
	u64 size_c = (u64)rows_a * cols_b * sizeof(float);

	// Allocate VRAM for all three matrices
	u64 vram_a = gpu_mem_alloc(size_a);
	u64 vram_b = gpu_mem_alloc(size_b);
	u64 vram_c = gpu_mem_alloc(size_c);

	if (vram_a == 0xFFFFFFFFFFFFFFFF || vram_b == 0xFFFFFFFFFFFFFFFF ||
	    vram_c == 0xFFFFFFFFFFFFFFFF) {
		if (vram_a != 0xFFFFFFFFFFFFFFFF) gpu_mem_free(vram_a, size_a);
		if (vram_b != 0xFFFFFFFFFFFFFFFF) gpu_mem_free(vram_b, size_b);
		if (vram_c != 0xFFFFFFFFFFFFFFFF) gpu_mem_free(vram_c, size_c);
		return -1;
	}

	// Upload matrices A and B
	u64 fence_a = gpu_mem_copy_to(A, vram_a, size_a);
	u64 fence_b = gpu_mem_copy_to(B, vram_b, size_b);
	gpu_fence_wait(fence_a);
	gpu_fence_wait(fence_b);

	// Prepare compute parameters for matmul shader
	// Grid: one block per output tile (32x32)
	u32 tiles_x = (cols_b + 31) / 32;
	u32 tiles_y = (rows_a + 31) / 32;

	gpu_compute_params_t params;
	params.shader_addr = 0;		// Matmul shader (built-in kernel 0)
	params.grid_x = tiles_x;
	params.grid_y = tiles_y;
	params.grid_z = 1;
	params.block_x = 32;
	params.block_y = 32;
	params.block_z = 1;
	params.input_buffer = vram_a;
	params.output_buffer = vram_c;
	params.input_size = size_a;
	params.output_size = size_c;

	u64 fence = gpu_compute(&params);
	if (fence == 0xFFFFFFFF) {
		gpu_mem_free(vram_a, size_a);
		gpu_mem_free(vram_b, size_b);
		gpu_mem_free(vram_c, size_c);
		return -1;
	}
	gpu_fence_wait(fence);

	// Download result
	fence = gpu_mem_copy_from(vram_c, C, size_c);
	gpu_fence_wait(fence);

	// Cleanup
	gpu_mem_free(vram_a, size_a);
	gpu_mem_free(vram_b, size_b);
	gpu_mem_free(vram_c, size_c);

	return 0;
}

int gpu_dot_f32(float *a, float *b, u32 len, float *result) {
	if (!gpu_available()) return -1;

	u64 size = (u64)len * sizeof(float);
	u64 result_size = sizeof(float);

	u64 vram_a = gpu_mem_alloc(size);
	u64 vram_b = gpu_mem_alloc(size);
	u64 vram_r = gpu_mem_alloc(result_size);

	if (vram_a == 0xFFFFFFFFFFFFFFFF || vram_b == 0xFFFFFFFFFFFFFFFF ||
	    vram_r == 0xFFFFFFFFFFFFFFFF) {
		if (vram_a != 0xFFFFFFFFFFFFFFFF) gpu_mem_free(vram_a, size);
		if (vram_b != 0xFFFFFFFFFFFFFFFF) gpu_mem_free(vram_b, size);
		if (vram_r != 0xFFFFFFFFFFFFFFFF) gpu_mem_free(vram_r, result_size);
		return -1;
	}

	u64 f1 = gpu_mem_copy_to(a, vram_a, size);
	u64 f2 = gpu_mem_copy_to(b, vram_b, size);
	gpu_fence_wait(f1);
	gpu_fence_wait(f2);

	// Dispatch dot product compute
	gpu_compute_params_t params;
	params.shader_addr = 1;		// Dot product shader (built-in kernel 1)
	params.grid_x = (len + 255) / 256;
	params.grid_y = 1;
	params.grid_z = 1;
	params.block_x = 256;
	params.block_y = 1;
	params.block_z = 1;
	params.input_buffer = vram_a;
	params.output_buffer = vram_r;
	params.input_size = size;
	params.output_size = result_size;

	u64 fence = gpu_compute(&params);
	if (fence == 0xFFFFFFFF) {
		gpu_mem_free(vram_a, size);
		gpu_mem_free(vram_b, size);
		gpu_mem_free(vram_r, result_size);
		return -1;
	}
	gpu_fence_wait(fence);

	fence = gpu_mem_copy_from(vram_r, result, result_size);
	gpu_fence_wait(fence);

	gpu_mem_free(vram_a, size);
	gpu_mem_free(vram_b, size);
	gpu_mem_free(vram_r, result_size);

	return 0;
}

int gpu_relu_f32(float *data, u32 len) {
	if (!gpu_available()) return -1;

	u64 size = (u64)len * sizeof(float);
	u64 vram = gpu_mem_alloc(size);
	if (vram == 0xFFFFFFFFFFFFFFFF) return -1;

	u64 fence = gpu_mem_copy_to(data, vram, size);
	gpu_fence_wait(fence);

	gpu_compute_params_t params;
	params.shader_addr = 2;		// ReLU shader (built-in kernel 2)
	params.grid_x = (len + 255) / 256;
	params.grid_y = 1;
	params.grid_z = 1;
	params.block_x = 256;
	params.block_y = 1;
	params.block_z = 1;
	params.input_buffer = vram;
	params.output_buffer = vram;	// In-place
	params.input_size = size;
	params.output_size = size;

	fence = gpu_compute(&params);
	if (fence == 0xFFFFFFFF) { gpu_mem_free(vram, size); return -1; }
	gpu_fence_wait(fence);

	fence = gpu_mem_copy_from(vram, data, size);
	gpu_fence_wait(fence);

	gpu_mem_free(vram, size);
	return 0;
}

int gpu_sigmoid_f32(float *data, u32 len) {
	if (!gpu_available()) return -1;

	u64 size = (u64)len * sizeof(float);
	u64 vram = gpu_mem_alloc(size);
	if (vram == 0xFFFFFFFFFFFFFFFF) return -1;

	u64 fence = gpu_mem_copy_to(data, vram, size);
	gpu_fence_wait(fence);

	gpu_compute_params_t params;
	params.shader_addr = 3;		// Sigmoid shader (built-in kernel 3)
	params.grid_x = (len + 255) / 256;
	params.grid_y = 1;
	params.grid_z = 1;
	params.block_x = 256;
	params.block_y = 1;
	params.block_z = 1;
	params.input_buffer = vram;
	params.output_buffer = vram;
	params.input_size = size;
	params.output_size = size;

	fence = gpu_compute(&params);
	if (fence == 0xFFFFFFFF) { gpu_mem_free(vram, size); return -1; }
	gpu_fence_wait(fence);

	fence = gpu_mem_copy_from(vram, data, size);
	gpu_fence_wait(fence);

	gpu_mem_free(vram, size);
	return 0;
}

int gpu_softmax_f32(float *data, u32 len) {
	if (!gpu_available()) return -1;

	u64 size = (u64)len * sizeof(float);
	u64 vram = gpu_mem_alloc(size);
	if (vram == 0xFFFFFFFFFFFFFFFF) return -1;

	u64 fence = gpu_mem_copy_to(data, vram, size);
	gpu_fence_wait(fence);

	gpu_compute_params_t params;
	params.shader_addr = 4;		// Softmax shader (built-in kernel 4)
	params.grid_x = 1;		// Softmax is reduction, single group
	params.grid_y = 1;
	params.grid_z = 1;
	params.block_x = 256;
	params.block_y = 1;
	params.block_z = 1;
	params.input_buffer = vram;
	params.output_buffer = vram;
	params.input_size = size;
	params.output_size = size;

	fence = gpu_compute(&params);
	if (fence == 0xFFFFFFFF) { gpu_mem_free(vram, size); return -1; }
	gpu_fence_wait(fence);

	fence = gpu_mem_copy_from(vram, data, size);
	gpu_fence_wait(fence);

	gpu_mem_free(vram, size);
	return 0;
}


// ============================================================================
// Tensor Operations
// ============================================================================

static u64 tensor_total_elements(gpu_tensor_t *t) {
	u64 count = 1;
	for (u32 i = 0; i < t->ndims; i++) {
		count *= t->dims[i];
	}
	return count;
}

int gpu_tensor_upload(gpu_tensor_t *t) {
	u64 size = tensor_total_elements(t) * sizeof(float);

	if (t->vram_offset == 0) {
		t->vram_offset = gpu_mem_alloc(size);
		if (t->vram_offset == 0xFFFFFFFFFFFFFFFF) {
			t->vram_offset = 0;
			return -1;
		}
	}

	u64 fence = gpu_mem_copy_to(t->data, t->vram_offset, size);
	gpu_fence_wait(fence);
	return 0;
}

int gpu_tensor_download(gpu_tensor_t *t) {
	if (t->vram_offset == 0) return -1;

	u64 size = tensor_total_elements(t) * sizeof(float);
	u64 fence = gpu_mem_copy_from(t->vram_offset, t->data, size);
	gpu_fence_wait(fence);
	return 0;
}

void gpu_tensor_free(gpu_tensor_t *t) {
	if (t->vram_offset != 0) {
		u64 size = tensor_total_elements(t) * sizeof(float);
		gpu_mem_free(t->vram_offset, size);
		t->vram_offset = 0;
	}
}


// ============================================================================
// Evolution Engine GPU Functions
// ============================================================================

int gpu_evolve_evaluate(gpu_genome_t *population, u32 pop_size, u64 fitness_shader) {
	if (!gpu_available()) return -1;

	// Upload all genomes to VRAM as a contiguous buffer
	u64 genome_size = population[0].genome_size;
	u64 total_size = (u64)pop_size * genome_size;

	u64 vram_genomes = gpu_mem_alloc(total_size);
	u64 vram_fitness = gpu_mem_alloc((u64)pop_size * sizeof(float));

	if (vram_genomes == 0xFFFFFFFFFFFFFFFF || vram_fitness == 0xFFFFFFFFFFFFFFFF) {
		if (vram_genomes != 0xFFFFFFFFFFFFFFFF) gpu_mem_free(vram_genomes, total_size);
		if (vram_fitness != 0xFFFFFFFFFFFFFFFF) gpu_mem_free(vram_fitness, (u64)pop_size * sizeof(float));
		return -1;
	}

	// Upload each genome
	for (u32 i = 0; i < pop_size; i++) {
		u64 fence = gpu_mem_copy_to(
			population[i].genome,
			vram_genomes + i * genome_size,
			genome_size
		);
		gpu_fence_wait(fence);
	}

	// Dispatch fitness evaluation - one thread per genome
	gpu_compute_params_t params;
	params.shader_addr = fitness_shader;
	params.grid_x = (pop_size + 255) / 256;
	params.grid_y = 1;
	params.grid_z = 1;
	params.block_x = 256;
	params.block_y = 1;
	params.block_z = 1;
	params.input_buffer = vram_genomes;
	params.output_buffer = vram_fitness;
	params.input_size = total_size;
	params.output_size = (u64)pop_size * sizeof(float);

	u64 fence = gpu_compute(&params);
	if (fence == 0xFFFFFFFF) {
		gpu_mem_free(vram_genomes, total_size);
		gpu_mem_free(vram_fitness, (u64)pop_size * sizeof(float));
		return -1;
	}
	gpu_fence_wait(fence);

	// Download fitness scores
	float *fitness_buffer = (float *)population;  // Temporary reuse
	// Actually, let's download one by one to each genome's fitness field
	for (u32 i = 0; i < pop_size; i++) {
		fence = gpu_mem_copy_from(
			vram_fitness + i * sizeof(float),
			&population[i].fitness,
			sizeof(float)
		);
		gpu_fence_wait(fence);
	}

	gpu_mem_free(vram_genomes, total_size);
	gpu_mem_free(vram_fitness, (u64)pop_size * sizeof(float));

	return 0;
}

int gpu_evolve_crossover(gpu_genome_t *parents, u32 parent_count,
			  gpu_genome_t *offspring, u32 offspring_count,
			  float mutation_rate, u64 rng_seed) {
	if (!gpu_available()) return -1;

	u64 genome_size = parents[0].genome_size;
	u64 parents_total = (u64)parent_count * genome_size;
	u64 offspring_total = (u64)offspring_count * genome_size;

	u64 vram_parents = gpu_mem_alloc(parents_total);
	u64 vram_offspring = gpu_mem_alloc(offspring_total);

	if (vram_parents == 0xFFFFFFFFFFFFFFFF || vram_offspring == 0xFFFFFFFFFFFFFFFF) {
		if (vram_parents != 0xFFFFFFFFFFFFFFFF) gpu_mem_free(vram_parents, parents_total);
		if (vram_offspring != 0xFFFFFFFFFFFFFFFF) gpu_mem_free(vram_offspring, offspring_total);
		return -1;
	}

	// Upload parents
	for (u32 i = 0; i < parent_count; i++) {
		u64 fence = gpu_mem_copy_to(
			parents[i].genome,
			vram_parents + i * genome_size,
			genome_size
		);
		gpu_fence_wait(fence);
	}

	// Dispatch crossover/mutation kernel
	gpu_compute_params_t params;
	params.shader_addr = 5;		// Crossover/mutation shader (built-in kernel 5)
	params.grid_x = (offspring_count + 255) / 256;
	params.grid_y = 1;
	params.grid_z = 1;
	params.block_x = 256;
	params.block_y = 1;
	params.block_z = 1;
	params.input_buffer = vram_parents;
	params.output_buffer = vram_offspring;
	params.input_size = parents_total;
	params.output_size = offspring_total;

	u64 fence = gpu_compute(&params);
	if (fence == 0xFFFFFFFF) {
		gpu_mem_free(vram_parents, parents_total);
		gpu_mem_free(vram_offspring, offspring_total);
		return -1;
	}
	gpu_fence_wait(fence);

	// Download offspring
	for (u32 i = 0; i < offspring_count; i++) {
		fence = gpu_mem_copy_from(
			vram_offspring + i * genome_size,
			offspring[i].genome,
			genome_size
		);
		gpu_fence_wait(fence);
	}

	gpu_mem_free(vram_parents, parents_total);
	gpu_mem_free(vram_offspring, offspring_total);

	return 0;
}

int gpu_evolve_select(gpu_genome_t *population, u32 pop_size,
		       gpu_genome_t *selected, u32 select_count) {
	// Simple tournament selection (CPU-side, fast enough)
	// Sort by fitness (descending) and pick top select_count
	for (u32 i = 0; i < pop_size - 1; i++) {
		for (u32 j = i + 1; j < pop_size; j++) {
			if (population[j].fitness > population[i].fitness) {
				// Swap
				gpu_genome_t tmp = population[i];
				population[i] = population[j];
				population[j] = tmp;
			}
		}
	}

	// Copy top performers
	for (u32 i = 0; i < select_count && i < pop_size; i++) {
		selected[i] = population[i];
	}

	return 0;
}


// =============================================================================
// EOF
