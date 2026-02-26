/*
 * x86-64 Opcode Tables
 * Maps opcodes to instruction lengths, flags, and properties
 */
#ifndef X86_TABLES_H
#define X86_TABLES_H

#include <stdint.h>

/* Opcode flags */
#define OP_NONE      0x00
#define OP_MODRM     0x01  /* Has ModR/M byte */
#define OP_IMM8      0x02  /* 8-bit immediate */
#define OP_IMM16     0x04  /* 16-bit immediate */
#define OP_IMM32     0x08  /* 32-bit immediate */
#define OP_IMM64     0x10  /* 64-bit immediate (MOV r64, imm64) */
#define OP_REL8      0x20  /* 8-bit relative offset */
#define OP_REL32     0x40  /* 32-bit relative offset */
#define OP_PREFIX    0x80  /* This is a prefix byte */

/* Opcode entry */
typedef struct {
    uint8_t  base_len;    /* Minimum instruction length (opcode only) */
    uint8_t  flags;       /* OP_* flags */
    uint8_t  is_branch;   /* 1 if branch/jump/call/ret */
    uint8_t  is_ret;      /* 1 if RET/IRETQ */
    uint8_t  is_nop;      /* 1 if NOP */
    uint8_t  is_push;     /* 1 if PUSH */
    uint8_t  is_pop;      /* 1 if POP */
} opcode_entry_t;

/* Primary opcode table (256 entries) */
extern const opcode_entry_t opcode_table_1byte[256];

/* Two-byte opcode table (0F xx, 256 entries) */
extern const opcode_entry_t opcode_table_2byte[256];

/* Get ModR/M extra bytes (SIB + displacement) */
int modrm_extra_bytes(uint8_t modrm, int addr_size_override);

#endif /* X86_TABLES_H */
