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
     * AlJefra OS hybrid image layout (256MB):
     *   - FAT32 partition: 0-128MB
     *   - BMFS partition: 128MB+ (sector 32768 at 4K granularity)
     *   - MBR loads from LBA 262160 (512-byte sectors)
     *   - Pure64 boot loader at LBA 262160
     *   - Kernel at BMFS sector offset (sector 10 at 4K = byte 40960 into BMFS)
     *
     * For evolution: use the BIOS-only image if available, or patch
     * the kernel at the correct offset in the hybrid image.
     * BMFS starts at 128MB = offset 134217728. Kernel at sector 10
     * of BMFS = offset 134217728 + 10*4096 = 134258688.
     */
    char cmd[1024];
    #define BMFS_OFFSET      134217728UL  /* 128 MB */
    #define KERNEL_SECTOR    10           /* Kernel at BMFS sector 10 */
    #define KERNEL_IMG_OFF   (BMFS_OFFSET + KERNEL_SECTOR * 4096)
    snprintf(cmd, sizeof(cmd),
        "cp ../sys/aljefra_os.img %s/tmp/test.img 2>/dev/null && "
        "dd if=%s of=%s/tmp/test.img bs=1 seek=%lu conv=notrunc 2>/dev/null",
        work_dir, kernel_path, work_dir, KERNEL_IMG_OFF);

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

    /* Build QEMU command.
     * Key flags:
     *   -display none -monitor none  — headless, no interactive monitor
     *   -serial file:...             — capture serial output to file
     *   -no-reboot                   — exit on triple fault instead of rebooting
     *   -smp sockets=1,cpus=1        — avoid monitor/mwait UD on Westmere
     *   -daemonize                   — detach from terminal immediately
     * After daemonizing, sleep for timeout then kill.
     */
    char cmd[2048];
    char pidfile[512];
    snprintf(pidfile, sizeof(pidfile), "%s/tmp/qemu.pid", work_dir);
    snprintf(cmd, sizeof(cmd),
        "%s -machine %s -cpu %s -smp sockets=1,cpus=1 -m %s "
        "-drive file=%s,format=raw,if=ide "
        "-serial file:%s "
        "-display none "
        "-monitor none "
        "-no-reboot "
        "-pidfile %s "
        "-daemonize 2>/dev/null; "
        "sleep %d; "
        "if [ -f %s ]; then kill $(cat %s) 2>/dev/null; rm -f %s; fi",
        QEMU_BINARY, QEMU_MACHINE, QEMU_CPU, QEMU_MEMORY,
        img_path, serial_path,
        pidfile,
        timeout_secs,
        pidfile, pidfile, pidfile);

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
