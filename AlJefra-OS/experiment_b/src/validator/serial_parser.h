/*
 * Serial Output Parser
 * Parses AlJefra OS benchmark results from QEMU serial log
 */
#ifndef SERIAL_PARSER_H
#define SERIAL_PARSER_H

#include "../config.h"

/* Parse a serial log file for benchmark results.
 * Returns 1 if successfully parsed, 0 otherwise. */
int serial_parse(const char *log_path, benchmark_result_t *out);

/* Check if serial output indicates successful boot */
int serial_check_boot(const char *log_path);

#endif /* SERIAL_PARSER_H */
