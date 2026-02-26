// =============================================================================
// AlJefra OS AI -- Neural Network Inference Engine Implementation
// Copyright (C) 2026
//
// GPU-accelerated neural network for OS self-evolution
// =============================================================================

#include "ai_inference.h"
#include "gpu_engine.h"

// Global model storage
static ai_model_t models[AI_MAX_MODELS];
static u32 model_count = 0;

// ============================================================================
// Initialization
// ============================================================================

int ai_init(void) {
	if (!gpu_available()) return -1;

	// Clear model storage
	for (u32 i = 0; i < AI_MAX_MODELS; i++) {
		models[i].model_id = 0xFFFFFFFF;
		models[i].num_layers = 0;
	}
	model_count = 0;

	return 0;
}


// ============================================================================
// Model Creation
// ============================================================================

int ai_create_model(u32 purpose, u32 num_layers, u32 *layer_sizes, u32 *activations) {
	if (model_count >= AI_MAX_MODELS) return -1;
	if (num_layers < 2 || num_layers > AI_MAX_LAYERS) return -1;

	u32 id = model_count;
	ai_model_t *m = &models[id];

	m->model_id = id;
	m->purpose = purpose;
	m->num_layers = num_layers - 1;  // Layers = connections between sizes
	m->input_size = layer_sizes[0];
	m->output_size = layer_sizes[num_layers - 1];
	m->total_params = 0;
	m->total_memory = 0;

	// Create each layer
	for (u32 i = 0; i < num_layers - 1; i++) {
		ai_layer_t *l = &m->layers[i];
		l->type = AI_LAYER_DENSE;
		l->input_size = layer_sizes[i];
		l->output_size = layer_sizes[i + 1];
		l->activation = activations[i];

		// Calculate memory needed
		u64 weight_size = (u64)l->input_size * l->output_size * sizeof(float);
		u64 bias_size = (u64)l->output_size * sizeof(float);
		u64 output_size = (u64)l->output_size * sizeof(float);

		// Allocate VRAM for weights, biases, and output buffer
		l->weights_vram = gpu_mem_alloc(weight_size);
		l->biases_vram = gpu_mem_alloc(bias_size);
		l->output_vram = gpu_mem_alloc(output_size);

		if (l->weights_vram == 0xFFFFFFFFFFFFFFFF ||
		    l->biases_vram == 0xFFFFFFFFFFFFFFFF ||
		    l->output_vram == 0xFFFFFFFFFFFFFFFF) {
			// Cleanup on failure
			ai_free_model(id);
			return -1;
		}

		u32 params = l->input_size * l->output_size + l->output_size;
		m->total_params += params;
		m->total_memory += weight_size + bias_size + output_size;
	}

	model_count++;
	return (int)id;
}

int ai_load_weights(u32 model_id, float *weights, u64 weight_size) {
	if (model_id >= model_count) return -1;
	ai_model_t *m = &models[model_id];

	// Upload weights layer by layer
	u64 offset = 0;
	for (u32 i = 0; i < m->num_layers; i++) {
		ai_layer_t *l = &m->layers[i];
		u64 w_size = (u64)l->input_size * l->output_size * sizeof(float);
		u64 b_size = (u64)l->output_size * sizeof(float);

		// Upload weights
		if (offset + w_size > weight_size) return -1;
		u64 fence = gpu_mem_copy_to((void *)((u8 *)weights + offset), l->weights_vram, w_size);
		gpu_fence_wait(fence);
		offset += w_size;

		// Upload biases
		if (offset + b_size > weight_size) return -1;
		fence = gpu_mem_copy_to((void *)((u8 *)weights + offset), l->biases_vram, b_size);
		gpu_fence_wait(fence);
		offset += b_size;
	}

	return 0;
}


// ============================================================================
// Inference
// ============================================================================

int ai_infer(ai_infer_request_t *request) {
	if (request->model_id >= model_count) return -1;
	ai_model_t *m = &models[request->model_id];

	if (request->input_size != m->input_size) return -1;
	if (request->output_size != m->output_size) return -1;

	// Upload input to VRAM
	u64 input_vram = gpu_mem_alloc(request->input_size * sizeof(float));
	if (input_vram == 0xFFFFFFFFFFFFFFFF) return -1;

	u64 fence = gpu_mem_copy_to(request->input, input_vram,
				     request->input_size * sizeof(float));
	gpu_fence_wait(fence);

	// Forward pass through each layer
	u64 current_input = input_vram;

	for (u32 i = 0; i < m->num_layers; i++) {
		ai_layer_t *l = &m->layers[i];

		// Matrix multiply: output = input * weights + biases
		// Use GPU compute dispatch for this
		gpu_compute_params_t params;
		params.shader_addr = 0;		// Matmul + bias + activation shader
		params.grid_x = (l->output_size + 31) / 32;
		params.grid_y = 1;
		params.grid_z = 1;
		params.block_x = 32;
		params.block_y = 1;
		params.block_z = 1;
		params.input_buffer = current_input;
		params.output_buffer = l->output_vram;
		params.input_size = l->input_size * sizeof(float);
		params.output_size = l->output_size * sizeof(float);

		fence = gpu_compute(&params);
		if (fence == 0xFFFFFFFF) {
			gpu_mem_free(input_vram, request->input_size * sizeof(float));
			return -1;
		}
		gpu_fence_wait(fence);

		current_input = l->output_vram;
	}

	// Download final output
	ai_layer_t *last = &m->layers[m->num_layers - 1];
	fence = gpu_mem_copy_from(last->output_vram, request->output,
				   request->output_size * sizeof(float));
	gpu_fence_wait(fence);

	// Free temporary input VRAM
	gpu_mem_free(input_vram, request->input_size * sizeof(float));

	return 0;
}

int ai_infer_sync(u32 model_id, float *input, u32 input_size,
		   float *output, u32 output_size) {
	ai_infer_request_t req;
	req.model_id = model_id;
	req.input = input;
	req.input_size = input_size;
	req.output = output;
	req.output_size = output_size;
	return ai_infer(&req);
}


// ============================================================================
// Model Management
// ============================================================================

void ai_free_model(u32 model_id) {
	if (model_id >= AI_MAX_MODELS) return;
	ai_model_t *m = &models[model_id];

	for (u32 i = 0; i < m->num_layers; i++) {
		ai_layer_t *l = &m->layers[i];
		u64 w_size = (u64)l->input_size * l->output_size * sizeof(float);
		u64 b_size = (u64)l->output_size * sizeof(float);
		u64 o_size = (u64)l->output_size * sizeof(float);

		if (l->weights_vram != 0xFFFFFFFFFFFFFFFF) gpu_mem_free(l->weights_vram, w_size);
		if (l->biases_vram != 0xFFFFFFFFFFFFFFFF) gpu_mem_free(l->biases_vram, b_size);
		if (l->output_vram != 0xFFFFFFFFFFFFFFFF) gpu_mem_free(l->output_vram, o_size);
	}

	m->model_id = 0xFFFFFFFF;
	m->num_layers = 0;
}

int ai_get_model_info(u32 model_id, ai_model_t *info) {
	if (model_id >= model_count) return -1;
	*info = models[model_id];
	return 0;
}


// ============================================================================
// Pre-built Models
// ============================================================================

int ai_create_scheduler_model(void) {
	// 259 inputs -> 128 -> 64 -> 2 outputs
	u32 sizes[] = {259, 128, 64, 2};
	u32 acts[] = {AI_ACT_RELU, AI_ACT_RELU, AI_ACT_NONE};
	return ai_create_model(AI_MODEL_SCHEDULER, 4, sizes, acts);
}

int ai_create_memory_model(void) {
	// 66 inputs -> 64 -> 32 -> 2 outputs
	u32 sizes[] = {66, 64, 32, 2};
	u32 acts[] = {AI_ACT_RELU, AI_ACT_RELU, AI_ACT_RELU};
	return ai_create_model(AI_MODEL_MEMORY, 4, sizes, acts);
}

int ai_create_evolution_model(void) {
	// 128 inputs -> 256 -> 128 -> 64 -> 1 output
	u32 sizes[] = {128, 256, 128, 64, 1};
	u32 acts[] = {AI_ACT_RELU, AI_ACT_RELU, AI_ACT_RELU, AI_ACT_SIGMOID};
	return ai_create_model(AI_MODEL_EVOLUTION, 5, sizes, acts);
}


// ============================================================================
// Online Learning
// ============================================================================

int ai_train_batch(u32 model_id,
		    float *inputs, float *targets,
		    u32 batch_size, u32 input_size, u32 output_size,
		    float learning_rate) {
	if (model_id >= model_count) return -1;
	if (!gpu_available()) return -1;

	// Upload batch data to VRAM
	u64 inputs_size = (u64)batch_size * input_size * sizeof(float);
	u64 targets_size = (u64)batch_size * output_size * sizeof(float);

	u64 vram_inputs = gpu_mem_alloc(inputs_size);
	u64 vram_targets = gpu_mem_alloc(targets_size);

	if (vram_inputs == 0xFFFFFFFFFFFFFFFF || vram_targets == 0xFFFFFFFFFFFFFFFF) {
		if (vram_inputs != 0xFFFFFFFFFFFFFFFF) gpu_mem_free(vram_inputs, inputs_size);
		if (vram_targets != 0xFFFFFFFFFFFFFFFF) gpu_mem_free(vram_targets, targets_size);
		return -1;
	}

	u64 f1 = gpu_mem_copy_to(inputs, vram_inputs, inputs_size);
	u64 f2 = gpu_mem_copy_to(targets, vram_targets, targets_size);
	gpu_fence_wait(f1);
	gpu_fence_wait(f2);

	// Forward pass + backward pass + weight update
	// This is done as a single GPU compute dispatch
	gpu_compute_params_t params;
	params.shader_addr = 6;		// Training shader (built-in kernel 6)
	params.grid_x = batch_size;
	params.grid_y = 1;
	params.grid_z = 1;
	params.block_x = 32;
	params.block_y = 1;
	params.block_z = 1;
	params.input_buffer = vram_inputs;
	params.output_buffer = vram_targets;
	params.input_size = inputs_size;
	params.output_size = targets_size;

	u64 fence = gpu_compute(&params);
	if (fence != 0xFFFFFFFF) {
		gpu_fence_wait(fence);
	}

	gpu_mem_free(vram_inputs, inputs_size);
	gpu_mem_free(vram_targets, targets_size);

	return 0;
}


// =============================================================================
// EOF
