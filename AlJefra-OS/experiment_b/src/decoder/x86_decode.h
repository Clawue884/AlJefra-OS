/*
 * x86-64 Instruction Decoder
 * Determines instruction boundaries and properties from raw binary
 */
#ifndef X86_DECODE_H
#define X86_DECODE_H

#include "../config.h"

/* Decode a single instruction at the given address.
 * Returns the number of bytes consumed, or 0 on error. */
int x86_decode_instruction(const uint8_t *code, size_t max_len,
                           uint64_t address, instruction_t *out);

/* Decode all instructions in a binary buffer.
 * Returns the number of instructions decoded. */
int x86_decode_all(const uint8_t *code, size_t code_len,
                   uint64_t base_address,
                   instruction_t *out, int max_instructions);

/* Check if an instruction modifies the stack pointer */
int x86_modifies_sp(const instruction_t *instr);

/* Check if instruction is a memory reference (has ModR/M with memory operand) */
int x86_has_mem_operand(const instruction_t *instr);

/* Get the register encoded in ModR/M reg field */
int x86_get_modrm_reg(const instruction_t *instr);

#endif /* X86_DECODE_H */
