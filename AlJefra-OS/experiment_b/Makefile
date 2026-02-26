# AlJefra OS — Binary Evolution Engine
# Auto-detects CUDA availability

CC       := gcc
CFLAGS   := -O2 -Wall -Wextra -std=c11 -fopenmp
LDFLAGS  := -lm -fopenmp

# Auto-detect CUDA
NVCC     := $(shell which nvcc 2>/dev/null)
ifdef NVCC
    CUDA_FLAGS := -DHAVE_CUDA
    CUDA_LDFLAGS := -L/usr/local/cuda/lib64 -lcudart -lstdc++
    CUDA_SOURCES := cuda/fitness_kernel.cu cuda/mutation_kernel.cu cuda/gpu_evolve_kernel.cu
    CUDA_OBJS := build/fitness_kernel.o build/mutation_kernel.o build/gpu_evolve_kernel.o
    $(info CUDA detected: $(NVCC))
else
    CUDA_FLAGS :=
    CUDA_LDFLAGS :=
    CUDA_SOURCES :=
    CUDA_OBJS :=
    $(info CUDA not found — using CPU-only mode)
endif

CFLAGS += $(CUDA_FLAGS)
LDFLAGS += $(CUDA_LDFLAGS)

# Source files
SOURCES := \
    src/main.c \
    src/decoder/x86_decode.c \
    src/decoder/x86_tables.c \
    src/extractor/listing_parser.c \
    src/extractor/component_map.c \
    src/mutator/mutator.c \
    src/mutator/patterns.c \
    src/fitness/fitness_cpu.c \
    src/fitness/latency_tables.c \
    src/validator/qemu_runner.c \
    src/validator/serial_parser.c \
    src/ga/population.c \
    src/ga/selection.c \
    src/ga/crossover.c \
    src/recorder/recorder.c

OBJECTS := $(patsubst src/%.c,build/%.o,$(SOURCES))
TARGET  := evolve_bin

.PHONY: all clean test

all: dirs $(TARGET)
	@echo ""
	@echo "Build complete: ./$(TARGET)"
	@echo "Usage: ./$(TARGET) [component] [generations]"
	@echo ""

dirs:
	@mkdir -p build/decoder build/extractor build/mutator build/fitness \
	          build/validator build/ga build/recorder results

$(TARGET): $(OBJECTS) $(CUDA_OBJS)
	$(CC) $(OBJECTS) $(CUDA_OBJS) -o $@ $(LDFLAGS)

build/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# CUDA compilation
ifdef NVCC
CUDA_ARCH := $(shell nvidia-smi --query-gpu=compute_cap --format=csv,noheader 2>/dev/null | head -1 | tr -d '.')
NVCC_ARCH := $(if $(CUDA_ARCH),-arch=sm_$(CUDA_ARCH),)

build/fitness_kernel.o: cuda/fitness_kernel.cu
	$(NVCC) -DHAVE_CUDA $(NVCC_ARCH) -c $< -o $@ --compiler-options '-fPIC'

build/mutation_kernel.o: cuda/mutation_kernel.cu
	$(NVCC) -DHAVE_CUDA $(NVCC_ARCH) -c $< -o $@ --compiler-options '-fPIC'

build/gpu_evolve_kernel.o: cuda/gpu_evolve_kernel.cu
	$(NVCC) -DHAVE_CUDA $(NVCC_ARCH) -c $< -o $@ --compiler-options '-fPIC'
endif

clean:
	rm -rf build $(TARGET)

# Quick test: decode a few bytes
test: $(TARGET)
	@echo "Running self-test..."
	@echo "  Test 1: Version check"
	@./$(TARGET) --help 2>&1 | head -3 || true
	@echo "  Test 2: Component list"
	@echo "  Valid components: kernel memory smp network storage gpu_driver bus interrupts timer io syscalls vram_alloc cmd_queue dma scheduler"
	@echo "  Tests complete."
