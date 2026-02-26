/*
 * Serial Output Parser
 * AlJefra OS outputs benchmark data over serial port.
 * QEMU captures this to sys/serial.log.
 *
 * Expected format from b_evolve_benchmark syscall:
 *   BENCH:KERNEL_LATENCY:<value_us>
 *   BENCH:MEM_BANDWIDTH:<value_mbs>
 *   BENCH:SMP_SCALING:<value_pct>
 *   BENCH:NET_LATENCY:<value_us>
 *   BENCH:NVS_LATENCY:<value_us>
 *   BENCH:GPU_LATENCY:<value_us>
 *   BENCH:DONE
 *
 * Also detects boot success via "AlJefra OS AI" banner.
 */
#include "serial_parser.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int serial_parse(const char *log_path, benchmark_result_t *out) {
    FILE *f = fopen(log_path, "r");
    if (!f) return 0;

    memset(out, 0, sizeof(*out));
    char line[512];
    int found_any = 0;

    while (fgets(line, sizeof(line), f)) {
        /* Check for boot success */
        if (strstr(line, "AlJefra") || strstr(line, "aljefra")) {
            out->boot_success = 1;
        }

        /* Parse benchmark lines */
        char *bench = strstr(line, "BENCH:");
        if (!bench) continue;
        bench += 6;  /* Skip "BENCH:" */

        if (strncmp(bench, "KERNEL_LATENCY:", 15) == 0) {
            out->kernel_latency_us = atof(bench + 15);
            found_any = 1;
        } else if (strncmp(bench, "MEM_BANDWIDTH:", 14) == 0) {
            out->mem_bandwidth_mbs = atof(bench + 14);
            found_any = 1;
        } else if (strncmp(bench, "SMP_SCALING:", 12) == 0) {
            out->smp_scaling_pct = atof(bench + 12);
            found_any = 1;
        } else if (strncmp(bench, "NET_LATENCY:", 12) == 0) {
            out->net_latency_us = atof(bench + 12);
            found_any = 1;
        } else if (strncmp(bench, "NVS_LATENCY:", 12) == 0) {
            out->nvs_latency_us = atof(bench + 12);
            found_any = 1;
        } else if (strncmp(bench, "GPU_LATENCY:", 12) == 0) {
            out->gpu_latency_us = atof(bench + 12);
            found_any = 1;
        } else if (strncmp(bench, "DONE", 4) == 0) {
            out->valid = 1;
        }
    }

    fclose(f);

    /* If we found benchmark data but no DONE marker, still consider it
     * valid if we got at least kernel latency */
    if (found_any && out->kernel_latency_us > 0) {
        out->valid = 1;
    }

    return out->valid;
}

int serial_check_boot(const char *log_path) {
    FILE *f = fopen(log_path, "r");
    if (!f) return 0;

    char line[512];
    int booted = 0;

    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, "AlJefra") || strstr(line, "aljefra") ||
            strstr(line, "OK") || strstr(line, "Ready")) {
            booted = 1;
            break;
        }
    }

    fclose(f);
    return booted;
}
