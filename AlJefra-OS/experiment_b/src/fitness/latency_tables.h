/*
 * Instruction Latency Tables
 * Based on Agner Fog's instruction tables for modern x86-64 CPUs
 * (Skylake/Ice Lake/Zen 3+)
 */
#ifndef LATENCY_TABLES_H
#define LATENCY_TABLES_H

#include <stdint.h>

/* Get predicted latency in cycles for a 1-byte opcode */
uint16_t latency_get_1byte(uint8_t opcode, uint8_t has_rex_w);

/* Get predicted latency for a 2-byte opcode (0F xx) */
uint16_t latency_get_2byte(uint8_t opcode2);

/* Get predicted throughput (reciprocal, in 0.1 cycle units) */
uint16_t throughput_get_1byte(uint8_t opcode, uint8_t has_rex_w);

/* Get throughput for 2-byte opcode */
uint16_t throughput_get_2byte(uint8_t opcode2);

#endif /* LATENCY_TABLES_H */
