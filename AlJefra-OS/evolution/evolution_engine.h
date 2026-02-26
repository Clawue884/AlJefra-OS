// =============================================================================
// AlJefra OS AI -- Self-Evolution Engine
// Copyright (C) 2026 -- see LICENSE.TXT
//
// The Evolution Engine continuously analyzes, benchmarks, and evolves
// every component of the OS using GPU-accelerated AI.
//
// Architecture:
//   1. BENCHMARK  - Measure current performance of each OS component
//   2. ANALYZE    - Use GPU to identify bottlenecks and optimization targets
//   3. GENERATE   - GPU-accelerated genetic algorithm generates candidate improvements
//   4. EVALUATE   - Test candidates in isolation
//   5. INTEGRATE  - Apply winning improvements
//   6. RECORD     - Log breakthrough with metrics, create fork
//   7. REPEAT     - Continue evolving
// =============================================================================

#ifndef _EVOLUTION_ENGINE_H
#define _EVOLUTION_ENGINE_H

#include "../gpu_engine/gpu_engine.h"

// ============================================================================
// Evolution Constants
// ============================================================================

#define EVO_MAX_COMPONENTS		16	// Maximum OS components to evolve
#define EVO_MAX_POPULATION		1024	// Maximum population per generation
#define EVO_MAX_GENOME_SIZE		65536	// Maximum genome size in bytes
#define EVO_MAX_BREAKTHROUGHS		4096	// Maximum recorded breakthroughs
#define EVO_BENCHMARK_ITERATIONS	100	// Iterations per benchmark

// Evolution status codes
#define EVO_STATUS_IDLE			0
#define EVO_STATUS_BENCHMARKING		1
#define EVO_STATUS_ANALYZING		2
#define EVO_STATUS_GENERATING		3
#define EVO_STATUS_EVALUATING		4
#define EVO_STATUS_INTEGRATING		5
#define EVO_STATUS_RECORDING		6

// Component IDs (what part of the OS we're evolving)
#define EVO_COMP_KERNEL			0	// Core kernel (interrupt, scheduler)
#define EVO_COMP_MEMORY			1	// Memory management
#define EVO_COMP_SMP			2	// Multi-core / SMP
#define EVO_COMP_NETWORK		3	// Network stack
#define EVO_COMP_STORAGE		4	// NVS / storage
#define EVO_COMP_GPU_DRIVER		5	// GPU driver itself
#define EVO_COMP_BUS			6	// PCIe / bus
#define EVO_COMP_INTERRUPTS		7	// Interrupt handling
#define EVO_COMP_TIMER			8	// Timer subsystem
#define EVO_COMP_IO			9	// I/O subsystem
#define EVO_COMP_SYSCALLS		10	// Syscall dispatch
#define EVO_COMP_VRAM_ALLOC		11	// VRAM allocator
#define EVO_COMP_CMD_QUEUE		12	// Command queue
#define EVO_COMP_DMA			13	// DMA engine
#define EVO_COMP_SCHEDULER		14	// AI workload scheduler
#define EVO_COMP_ALL			15	// Full system evolution

// Improvement threshold to qualify as a "breakthrough"
#define EVO_BREAKTHROUGH_THRESHOLD	5.0f	// 5% improvement = breakthrough


// ============================================================================
// Data Structures
// ============================================================================

// Benchmark results for a single component
typedef struct {
	u32 component_id;		// Which component
	u64 latency_ticks;		// Average latency in timer ticks
	u64 throughput;			// Throughput (bytes/sec or ops/sec)
	u64 efficiency;			// Efficiency percentage (0-100)
	u64 timestamp;			// When this benchmark was taken
	u32 iteration;			// Benchmark iteration count
} evo_benchmark_t;

// A single breakthrough record
typedef struct {
	u32 component_id;		// Component that improved
	u32 generation;			// Evolution generation
	float improvement_pct;		// Percentage improvement
	u64 old_metric;			// Previous best metric
	u64 new_metric;			// New best metric
	u64 timestamp;			// When breakthrough occurred
	char description[128];		// Human-readable description
	char fork_name[64];		// Git fork/branch name
} evo_breakthrough_t;

// Evolution state for one component
typedef struct {
	u32 component_id;
	u32 population_size;
	u32 genome_size;
	u32 generation;
	float best_fitness;
	float mutation_rate;
	u64 fitness_shader;		// VRAM address of fitness eval shader
	gpu_genome_t *population;	// Current population
	evo_benchmark_t baseline;	// Baseline performance before evolution
	evo_benchmark_t current;	// Current best performance
} evo_component_state_t;

// Full evolution engine state
typedef struct {
	u32 status;			// Current status (EVO_STATUS_*)
	u32 active_component;		// Which component is being evolved
	u32 total_generations;		// Total generations across all components
	u32 total_breakthroughs;	// Total breakthroughs achieved
	u64 start_time;			// Engine start timestamp
	u64 gpu_time_used;		// Total GPU ticks consumed
	evo_component_state_t components[EVO_MAX_COMPONENTS];
	evo_breakthrough_t breakthroughs[EVO_MAX_BREAKTHROUGHS];
} evo_engine_t;


// ============================================================================
// Evolution Engine API
// ============================================================================

// Initialize the evolution engine (requires GPU to be initialized first)
int evo_init(evo_engine_t *engine);

// Run one complete evolution cycle for a specific component
int evo_evolve_component(evo_engine_t *engine, u32 component_id);

// Run continuous evolution across all components
int evo_run_continuous(evo_engine_t *engine);

// Stop evolution
void evo_stop(evo_engine_t *engine);

// Get current status
u32 evo_get_status(evo_engine_t *engine);


// ============================================================================
// Benchmarking
// ============================================================================

// Run benchmarks for a specific component
int evo_benchmark_component(evo_engine_t *engine, u32 component_id, evo_benchmark_t *result);

// Run full system benchmark
int evo_benchmark_all(evo_engine_t *engine);


// ============================================================================
// Breakthrough Management
// ============================================================================

// Check if current performance qualifies as a breakthrough
int evo_check_breakthrough(evo_engine_t *engine, u32 component_id);

// Record a breakthrough (creates fork, logs metrics)
int evo_record_breakthrough(evo_engine_t *engine, evo_breakthrough_t *bt);

// Get breakthrough history
int evo_get_breakthroughs(evo_engine_t *engine, evo_breakthrough_t *out, u32 max_count, u32 *actual_count);


// ============================================================================
// Component-Specific Evolution Functions
// ============================================================================

// Kernel evolution: optimize interrupt dispatch, syscall routing
int evo_evolve_kernel(evo_engine_t *engine);

// Memory evolution: optimize allocation patterns, page table layout
int evo_evolve_memory(evo_engine_t *engine);

// SMP evolution: optimize core scheduling, workload distribution
int evo_evolve_smp(evo_engine_t *engine);

// Network evolution: optimize packet processing, buffer management
int evo_evolve_network(evo_engine_t *engine);

// Storage evolution: optimize NVS access patterns, caching
int evo_evolve_storage(evo_engine_t *engine);

// GPU driver evolution: optimize command submission, memory allocation
int evo_evolve_gpu(evo_engine_t *engine);


// ============================================================================
// Utility
// ============================================================================

// Print evolution status to serial/screen
void evo_print_status(evo_engine_t *engine);

// Print breakthrough log
void evo_print_breakthroughs(evo_engine_t *engine);

// Get evolution statistics as formatted string
void evo_get_stats_string(evo_engine_t *engine, char *buf, u32 buf_size);


#endif // _EVOLUTION_ENGINE_H


// =============================================================================
// EOF
