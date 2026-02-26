/*
 * QEMU Benchmark Runner
 * Boots mutated kernels in QEMU and measures real performance.
 *
 * Process:
 * 1. Write mutated kernel.bin to temp directory
 * 2. Build disk image using AlJefra OS build tools
 * 3. Launch QEMU with serial output capture
 * 4. Wait for benchmark completion or timeout
 * 5. Parse serial log for benchmark results
 */
#include "qemu_runner.h"
#include "serial_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>

/* Check if a command exists in PATH */
static int command_exists(const char *cmd) {
    char check[512];
    snprintf(check, sizeof(check), "which %s > /dev/null 2>&1", cmd);
    return system(check) == 0;
}

int qemu_init(const char *work_dir) {
    if (!command_exists(QEMU_BINARY)) {
        fprintf(stderr, "Error: %s not found. Install with:\n"
                "  sudo apt install qemu-system-x86_64\n", QEMU_BINARY);
        return -1;
    }

    if (!command_exists("nasm")) {
        fprintf(stderr, "Error: nasm not found. Install with:\n"
                "  sudo apt install nasm\n");
        return -1;
    }

    /* Create work directory if needed */
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "mkdir -p %s/tmp", work_dir);
    system(cmd);

    return 0;
}

int qemu_build_image(const uint8_t *kernel_bin, uint32_t kernel_size,
                     const char *work_dir) {
    /* Write the mutated kernel binary */
    char kernel_path[512];
    snprintf(kernel_path, sizeof(kernel_path), "%s/tmp/kernel.bin", work_dir);

    FILE *f = fopen(kernel_path, "wb");
    if (!f) {
        fprintf(stderr, "Error: Cannot write kernel to %s\n", kernel_path);
        return -1;
    }

    /* Pad to 64KB */
    fwrite(kernel_bin, 1, kernel_size, f);
    if (kernel_size < KERNEL_MAX_SIZE) {
        uint8_t zero = 0;
        for (uint32_t i = kernel_size; i < KERNEL_MAX_SIZE; i++) {
            fwrite(&zero, 1, 1, f);
        }
    }
    fclose(f);

    /* Build the disk image using aljefra.sh or direct dd.
     * The AlJefra OS disk image layout:
     *   Sector 0-2: Pure64 boot loader
     *   Sector 3+:  Kernel binary
     *
     * We need the existing boot sectors + our modified kernel.
     */
    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
        "cp ../sys/aljefra_os.img %s/tmp/test.img 2>/dev/null && "
        "dd if=%s of=%s/tmp/test.img bs=4096 seek=3 conv=notrunc 2>/dev/null",
        work_dir, kernel_path, work_dir);

    if (system(cmd) != 0) {
        /* Try alternative: build from scratch */
        snprintf(cmd, sizeof(cmd),
            "cd .. && ./aljefra.sh build 2>/dev/null");
        if (system(cmd) != 0) {
            fprintf(stderr, "Error: Failed to build disk image\n");
            return -1;
        }
        /* Copy the built image */
        snprintf(cmd, sizeof(cmd),
            "cp ../sys/aljefra_os.img %s/tmp/test.img",
            work_dir);
        system(cmd);
    }

    return 0;
}

int qemu_run_benchmark(const char *work_dir, int timeout_secs,
                       benchmark_result_t *result) {
    char img_path[512], serial_path[512];
    snprintf(img_path, sizeof(img_path), "%s/tmp/test.img", work_dir);
    snprintf(serial_path, sizeof(serial_path), "%s/tmp/serial.log", work_dir);

    /* Remove old serial log */
    unlink(serial_path);

    /* Build QEMU command */
    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
        "%s -machine %s -cpu %s -smp %s -m %s "
        "-drive file=%s,format=raw,if=virtio "
        "-device virtio-net-pci,netdev=net0 "
        "-netdev user,id=net0 "
        "-serial file:%s "
        "-display none "
        "-no-reboot "
        "-nographic "
        "& QEMU_PID=$!; "
        "sleep %d; "
        "kill $QEMU_PID 2>/dev/null; "
        "wait $QEMU_PID 2>/dev/null",
        QEMU_BINARY, QEMU_MACHINE, QEMU_CPU, QEMU_CORES, QEMU_MEMORY,
        img_path, serial_path, timeout_secs);

    int ret = system(cmd);
    (void)ret;

    /* Parse results */
    memset(result, 0, sizeof(*result));

    if (serial_parse(serial_path, result)) {
        result->composite_score = 0;  /* Will be calculated by caller */
        return 1;
    }

    /* Even if no benchmark data, check if it booted */
    result->boot_success = serial_check_boot(serial_path);
    return result->boot_success;
}

int qemu_validate(const uint8_t *kernel_bin, uint32_t kernel_size,
                  const char *work_dir, benchmark_result_t *result) {
    if (qemu_build_image(kernel_bin, kernel_size, work_dir) != 0) {
        return 0;
    }

    return qemu_run_benchmark(work_dir, QEMU_TIMEOUT_SECS, result);
}

void qemu_cleanup(const char *work_dir) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %s/tmp", work_dir);
    system(cmd);
}
