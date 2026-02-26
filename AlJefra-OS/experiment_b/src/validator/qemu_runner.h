/*
 * QEMU Benchmark Runner
 * Builds disk image, boots mutated kernel in QEMU, parses benchmarks
 */
#ifndef QEMU_RUNNER_H
#define QEMU_RUNNER_H

#include "../config.h"

/* Initialize the QEMU runner (check for QEMU binary, etc.) */
int qemu_init(const char *work_dir);

/* Build a bootable disk image with a mutated kernel binary.
 * Uses the AlJefra OS build system with the modified kernel injected. */
int qemu_build_image(const uint8_t *kernel_bin, uint32_t kernel_size,
                     const char *work_dir);

/* Run QEMU with the built image and collect benchmark results.
 * Blocks for up to timeout_secs seconds. */
int qemu_run_benchmark(const char *work_dir, int timeout_secs,
                       benchmark_result_t *result);

/* Full pipeline: build image + run benchmark + parse results */
int qemu_validate(const uint8_t *kernel_bin, uint32_t kernel_size,
                  const char *work_dir, benchmark_result_t *result);

/* Cleanup temporary files */
void qemu_cleanup(const char *work_dir);

#endif /* QEMU_RUNNER_H */
