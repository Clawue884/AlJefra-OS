// =============================================================================
// AlJefra OS AI -- Neural Network Inference Engine
// Copyright (C) 2026
//
// GPU-accelerated neural network inference for OS self-evolution.
// This engine runs small, fast neural networks on the RTX 5090 to:
// 1. Predict optimal OS parameters
// 2. Classify workload patterns
// 3. Guide the evolution genetic algorithm
// 4. Optimize real-time scheduling decisions
// =============================================================================

#ifndef _AI_INFERENCE_H
#define _AI_INFERENCE_H

#include "gpu_engine.h"

// ============================================================================
// Neural Network Types
// ============================================================================

#define AI_MAX_LAYERS		32
#define AI_MAX_NEURONS		4096
#define AI_MAX_MODELS		8

// Activation functions
#define AI_ACT_NONE		0
#define AI_ACT_RELU		1
#define AI_ACT_SIGMOID		2
#define AI_ACT_TANH		3
#define AI_ACT_SOFTMAX		4
#define AI_ACT_GELU		5

// Layer types
#define AI_LAYER_DENSE		0	// Fully connected
#define AI_LAYER_CONV1D		1	// 1D convolution (for time series)
#define AI_LAYER_RESIDUAL	2	// Residual connection
#define AI_LAYER_NORM		3	// Layer normalization
#define AI_LAYER_ATTENTION	4	// Self-attention (transformer)

// Model purpose
#define AI_MODEL_SCHEDULER	0	// Workload scheduling optimizer
#define AI_MODEL_MEMORY		1	// Memory allocation predictor
#define AI_MODEL_NETWORK	2	// Network traffic predictor
#define AI_MODEL_THERMAL	3	// Thermal/power predictor
#define AI_MODEL_EVOLUTION	4	// Evolution fitness predictor
#define AI_MODEL_ANOMALY	5	// Anomaly detection


// ============================================================================
// Data Structures
// ============================================================================

typedef struct {
	u32 type;		// AI_LAYER_*
	u32 input_size;		// Number of input neurons
	u32 output_size;	// Number of output neurons
	u32 activation;		// AI_ACT_*
	u64 weights_vram;	// VRAM offset of weight matrix
	u64 biases_vram;	// VRAM offset of bias vector
	u64 output_vram;	// VRAM offset of output buffer
} ai_layer_t;

typedef struct {
	u32 model_id;		// Unique model ID
	u32 purpose;		// AI_MODEL_*
	u32 num_layers;		// Number of layers
	u32 input_size;		// Total input size
	u32 output_size;	// Total output size
	u32 total_params;	// Total parameter count
	u64 total_memory;	// Total VRAM used
	ai_layer_t layers[AI_MAX_LAYERS];
} ai_model_t;

// Inference request
typedef struct {
	u32 model_id;
	float *input;		// Input data in system memory
	u32 input_size;
	float *output;		// Output buffer in system memory
	u32 output_size;
	u64 latency_ticks;	// Filled after inference
} ai_infer_request_t;


// ============================================================================
// API
// ============================================================================

// Initialize the AI inference engine
int ai_init(void);

// Create a neural network model
// Returns model_id, or -1 on failure
int ai_create_model(u32 purpose, u32 num_layers, u32 *layer_sizes, u32 *activations);

// Load pre-trained weights into a model
int ai_load_weights(u32 model_id, float *weights, u64 weight_size);

// Run inference
int ai_infer(ai_infer_request_t *request);

// Run inference synchronously (blocking)
int ai_infer_sync(u32 model_id, float *input, u32 input_size,
		   float *output, u32 output_size);

// Free a model
void ai_free_model(u32 model_id);

// Get model info
int ai_get_model_info(u32 model_id, ai_model_t *info);


// ============================================================================
// Pre-built Models for OS Evolution
// ============================================================================

// Create the default scheduler optimization model
// Input: [core_loads(256), queue_depth, workload_type, priority]
// Output: [optimal_core_id, estimated_latency]
int ai_create_scheduler_model(void);

// Create the memory prediction model
// Input: [alloc_history(64), current_usage, fragmentation]
// Output: [next_alloc_size, recommended_pool_size]
int ai_create_memory_model(void);

// Create the evolution fitness predictor
// Input: [genome_features(128)]
// Output: [predicted_fitness]
int ai_create_evolution_model(void);


// ============================================================================
// Online Learning (GPU-accelerated)
// ============================================================================

// Train a model with a batch of input/output pairs
int ai_train_batch(u32 model_id,
		    float *inputs, float *targets,
		    u32 batch_size, u32 input_size, u32 output_size,
		    float learning_rate);


#endif // _AI_INFERENCE_H


// =============================================================================
// EOF
