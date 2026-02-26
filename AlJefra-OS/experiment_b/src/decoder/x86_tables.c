/*
 * x86-64 Opcode Tables
 * Based on Intel SDM Vol. 2 opcode maps
 * Covers the instruction set used by AlJefra OS (64-bit mode)
 */
#include "x86_tables.h"

/* ── Primary Opcode Map (1-byte opcodes) ─────────────────────────── */
const opcode_entry_t opcode_table_1byte[256] = {
    /* 0x00-0x07: ADD */
    [0x00] = {1, OP_MODRM, 0,0,0,0,0},   /* ADD r/m8, r8 */
    [0x01] = {1, OP_MODRM, 0,0,0,0,0},   /* ADD r/m32, r32 */
    [0x02] = {1, OP_MODRM, 0,0,0,0,0},   /* ADD r8, r/m8 */
    [0x03] = {1, OP_MODRM, 0,0,0,0,0},   /* ADD r32, r/m32 */
    [0x04] = {1, OP_IMM8,  0,0,0,0,0},   /* ADD AL, imm8 */
    [0x05] = {1, OP_IMM32, 0,0,0,0,0},   /* ADD EAX, imm32 */
    [0x06] = {1, OP_NONE,  0,0,0,1,0},   /* PUSH ES (invalid 64-bit) */
    [0x07] = {1, OP_NONE,  0,0,0,0,1},   /* POP ES (invalid 64-bit) */

    /* 0x08-0x0F: OR */
    [0x08] = {1, OP_MODRM, 0,0,0,0,0},   /* OR r/m8, r8 */
    [0x09] = {1, OP_MODRM, 0,0,0,0,0},   /* OR r/m32, r32 */
    [0x0A] = {1, OP_MODRM, 0,0,0,0,0},   /* OR r8, r/m8 */
    [0x0B] = {1, OP_MODRM, 0,0,0,0,0},   /* OR r32, r/m32 */
    [0x0C] = {1, OP_IMM8,  0,0,0,0,0},   /* OR AL, imm8 */
    [0x0D] = {1, OP_IMM32, 0,0,0,0,0},   /* OR EAX, imm32 */
    [0x0E] = {1, OP_NONE,  0,0,0,1,0},   /* PUSH CS (invalid 64-bit) */
    [0x0F] = {1, OP_NONE,  0,0,0,0,0},   /* 2-byte escape (handled specially) */

    /* 0x10-0x17: ADC */
    [0x10] = {1, OP_MODRM, 0,0,0,0,0},
    [0x11] = {1, OP_MODRM, 0,0,0,0,0},
    [0x12] = {1, OP_MODRM, 0,0,0,0,0},
    [0x13] = {1, OP_MODRM, 0,0,0,0,0},
    [0x14] = {1, OP_IMM8,  0,0,0,0,0},
    [0x15] = {1, OP_IMM32, 0,0,0,0,0},
    [0x16] = {1, OP_NONE,  0,0,0,1,0},
    [0x17] = {1, OP_NONE,  0,0,0,0,1},

    /* 0x18-0x1F: SBB */
    [0x18] = {1, OP_MODRM, 0,0,0,0,0},
    [0x19] = {1, OP_MODRM, 0,0,0,0,0},
    [0x1A] = {1, OP_MODRM, 0,0,0,0,0},
    [0x1B] = {1, OP_MODRM, 0,0,0,0,0},
    [0x1C] = {1, OP_IMM8,  0,0,0,0,0},
    [0x1D] = {1, OP_IMM32, 0,0,0,0,0},
    [0x1E] = {1, OP_NONE,  0,0,0,1,0},
    [0x1F] = {1, OP_NONE,  0,0,0,0,1},

    /* 0x20-0x27: AND */
    [0x20] = {1, OP_MODRM, 0,0,0,0,0},
    [0x21] = {1, OP_MODRM, 0,0,0,0,0},
    [0x22] = {1, OP_MODRM, 0,0,0,0,0},
    [0x23] = {1, OP_MODRM, 0,0,0,0,0},
    [0x24] = {1, OP_IMM8,  0,0,0,0,0},
    [0x25] = {1, OP_IMM32, 0,0,0,0,0},
    [0x26] = {1, OP_PREFIX, 0,0,0,0,0},  /* ES segment override */
    [0x27] = {1, OP_NONE,  0,0,0,0,0},   /* DAA (invalid 64-bit) */

    /* 0x28-0x2F: SUB */
    [0x28] = {1, OP_MODRM, 0,0,0,0,0},
    [0x29] = {1, OP_MODRM, 0,0,0,0,0},
    [0x2A] = {1, OP_MODRM, 0,0,0,0,0},
    [0x2B] = {1, OP_MODRM, 0,0,0,0,0},
    [0x2C] = {1, OP_IMM8,  0,0,0,0,0},
    [0x2D] = {1, OP_IMM32, 0,0,0,0,0},
    [0x2E] = {1, OP_PREFIX, 0,0,0,0,0},  /* CS segment override */
    [0x2F] = {1, OP_NONE,  0,0,0,0,0},

    /* 0x30-0x37: XOR */
    [0x30] = {1, OP_MODRM, 0,0,0,0,0},
    [0x31] = {1, OP_MODRM, 0,0,0,0,0},
    [0x32] = {1, OP_MODRM, 0,0,0,0,0},
    [0x33] = {1, OP_MODRM, 0,0,0,0,0},
    [0x34] = {1, OP_IMM8,  0,0,0,0,0},
    [0x35] = {1, OP_IMM32, 0,0,0,0,0},
    [0x36] = {1, OP_PREFIX, 0,0,0,0,0},  /* SS segment override */
    [0x37] = {1, OP_NONE,  0,0,0,0,0},

    /* 0x38-0x3F: CMP */
    [0x38] = {1, OP_MODRM, 0,0,0,0,0},
    [0x39] = {1, OP_MODRM, 0,0,0,0,0},
    [0x3A] = {1, OP_MODRM, 0,0,0,0,0},
    [0x3B] = {1, OP_MODRM, 0,0,0,0,0},
    [0x3C] = {1, OP_IMM8,  0,0,0,0,0},
    [0x3D] = {1, OP_IMM32, 0,0,0,0,0},
    [0x3E] = {1, OP_PREFIX, 0,0,0,0,0},  /* DS segment override */
    [0x3F] = {1, OP_NONE,  0,0,0,0,0},

    /* 0x40-0x4F: REX prefixes (64-bit mode) */
    [0x40] = {1, OP_PREFIX, 0,0,0,0,0},
    [0x41] = {1, OP_PREFIX, 0,0,0,0,0},
    [0x42] = {1, OP_PREFIX, 0,0,0,0,0},
    [0x43] = {1, OP_PREFIX, 0,0,0,0,0},
    [0x44] = {1, OP_PREFIX, 0,0,0,0,0},
    [0x45] = {1, OP_PREFIX, 0,0,0,0,0},
    [0x46] = {1, OP_PREFIX, 0,0,0,0,0},
    [0x47] = {1, OP_PREFIX, 0,0,0,0,0},
    [0x48] = {1, OP_PREFIX, 0,0,0,0,0},  /* REX.W */
    [0x49] = {1, OP_PREFIX, 0,0,0,0,0},
    [0x4A] = {1, OP_PREFIX, 0,0,0,0,0},
    [0x4B] = {1, OP_PREFIX, 0,0,0,0,0},
    [0x4C] = {1, OP_PREFIX, 0,0,0,0,0},
    [0x4D] = {1, OP_PREFIX, 0,0,0,0,0},
    [0x4E] = {1, OP_PREFIX, 0,0,0,0,0},
    [0x4F] = {1, OP_PREFIX, 0,0,0,0,0},

    /* 0x50-0x57: PUSH r64 */
    [0x50] = {1, OP_NONE, 0,0,0,1,0},
    [0x51] = {1, OP_NONE, 0,0,0,1,0},
    [0x52] = {1, OP_NONE, 0,0,0,1,0},
    [0x53] = {1, OP_NONE, 0,0,0,1,0},
    [0x54] = {1, OP_NONE, 0,0,0,1,0},
    [0x55] = {1, OP_NONE, 0,0,0,1,0},
    [0x56] = {1, OP_NONE, 0,0,0,1,0},
    [0x57] = {1, OP_NONE, 0,0,0,1,0},

    /* 0x58-0x5F: POP r64 */
    [0x58] = {1, OP_NONE, 0,0,0,0,1},
    [0x59] = {1, OP_NONE, 0,0,0,0,1},
    [0x5A] = {1, OP_NONE, 0,0,0,0,1},
    [0x5B] = {1, OP_NONE, 0,0,0,0,1},
    [0x5C] = {1, OP_NONE, 0,0,0,0,1},
    [0x5D] = {1, OP_NONE, 0,0,0,0,1},
    [0x5E] = {1, OP_NONE, 0,0,0,0,1},
    [0x5F] = {1, OP_NONE, 0,0,0,0,1},

    /* 0x60-0x6F */
    [0x60] = {1, OP_NONE,  0,0,0,0,0},   /* PUSHA (invalid 64-bit) */
    [0x61] = {1, OP_NONE,  0,0,0,0,0},   /* POPA (invalid 64-bit) */
    [0x62] = {1, OP_MODRM, 0,0,0,0,0},   /* EVEX prefix / BOUND */
    [0x63] = {1, OP_MODRM, 0,0,0,0,0},   /* MOVSXD */
    [0x64] = {1, OP_PREFIX, 0,0,0,0,0},  /* FS segment override */
    [0x65] = {1, OP_PREFIX, 0,0,0,0,0},  /* GS segment override */
    [0x66] = {1, OP_PREFIX, 0,0,0,0,0},  /* Operand size override */
    [0x67] = {1, OP_PREFIX, 0,0,0,0,0},  /* Address size override */
    [0x68] = {1, OP_IMM32, 0,0,0,1,0},   /* PUSH imm32 */
    [0x69] = {1, OP_MODRM|OP_IMM32, 0,0,0,0,0}, /* IMUL r,r/m,imm32 */
    [0x6A] = {1, OP_IMM8,  0,0,0,1,0},   /* PUSH imm8 */
    [0x6B] = {1, OP_MODRM|OP_IMM8, 0,0,0,0,0},  /* IMUL r,r/m,imm8 */
    [0x6C] = {1, OP_NONE,  0,0,0,0,0},   /* INSB */
    [0x6D] = {1, OP_NONE,  0,0,0,0,0},   /* INSD */
    [0x6E] = {1, OP_NONE,  0,0,0,0,0},   /* OUTSB */
    [0x6F] = {1, OP_NONE,  0,0,0,0,0},   /* OUTSD */

    /* 0x70-0x7F: Jcc rel8 (short conditional jumps) */
    [0x70] = {1, OP_REL8, 1,0,0,0,0},    /* JO */
    [0x71] = {1, OP_REL8, 1,0,0,0,0},    /* JNO */
    [0x72] = {1, OP_REL8, 1,0,0,0,0},    /* JB/JNAE/JC */
    [0x73] = {1, OP_REL8, 1,0,0,0,0},    /* JNB/JAE/JNC */
    [0x74] = {1, OP_REL8, 1,0,0,0,0},    /* JZ/JE */
    [0x75] = {1, OP_REL8, 1,0,0,0,0},    /* JNZ/JNE */
    [0x76] = {1, OP_REL8, 1,0,0,0,0},    /* JBE/JNA */
    [0x77] = {1, OP_REL8, 1,0,0,0,0},    /* JNBE/JA */
    [0x78] = {1, OP_REL8, 1,0,0,0,0},    /* JS */
    [0x79] = {1, OP_REL8, 1,0,0,0,0},    /* JNS */
    [0x7A] = {1, OP_REL8, 1,0,0,0,0},    /* JP/JPE */
    [0x7B] = {1, OP_REL8, 1,0,0,0,0},    /* JNP/JPO */
    [0x7C] = {1, OP_REL8, 1,0,0,0,0},    /* JL/JNGE */
    [0x7D] = {1, OP_REL8, 1,0,0,0,0},    /* JNL/JGE */
    [0x7E] = {1, OP_REL8, 1,0,0,0,0},    /* JLE/JNG */
    [0x7F] = {1, OP_REL8, 1,0,0,0,0},    /* JNLE/JG */

    /* 0x80-0x83: Group 1 (ALU with immediate) */
    [0x80] = {1, OP_MODRM|OP_IMM8,  0,0,0,0,0},  /* GRP1 r/m8, imm8 */
    [0x81] = {1, OP_MODRM|OP_IMM32, 0,0,0,0,0},  /* GRP1 r/m32, imm32 */
    [0x82] = {1, OP_MODRM|OP_IMM8,  0,0,0,0,0},  /* GRP1 (alias, invalid 64-bit) */
    [0x83] = {1, OP_MODRM|OP_IMM8,  0,0,0,0,0},  /* GRP1 r/m32, imm8 */

    /* 0x84-0x8F */
    [0x84] = {1, OP_MODRM, 0,0,0,0,0},   /* TEST r/m8, r8 */
    [0x85] = {1, OP_MODRM, 0,0,0,0,0},   /* TEST r/m32, r32 */
    [0x86] = {1, OP_MODRM, 0,0,0,0,0},   /* XCHG r/m8, r8 */
    [0x87] = {1, OP_MODRM, 0,0,0,0,0},   /* XCHG r/m32, r32 */
    [0x88] = {1, OP_MODRM, 0,0,0,0,0},   /* MOV r/m8, r8 */
    [0x89] = {1, OP_MODRM, 0,0,0,0,0},   /* MOV r/m32, r32 */
    [0x8A] = {1, OP_MODRM, 0,0,0,0,0},   /* MOV r8, r/m8 */
    [0x8B] = {1, OP_MODRM, 0,0,0,0,0},   /* MOV r32, r/m32 */
    [0x8C] = {1, OP_MODRM, 0,0,0,0,0},   /* MOV r/m16, Sreg */
    [0x8D] = {1, OP_MODRM, 0,0,0,0,0},   /* LEA r32, m */
    [0x8E] = {1, OP_MODRM, 0,0,0,0,0},   /* MOV Sreg, r/m16 */
    [0x8F] = {1, OP_MODRM, 0,0,0,0,1},   /* POP r/m64 */

    /* 0x90-0x97: XCHG/NOP */
    [0x90] = {1, OP_NONE, 0,0,1,0,0},    /* NOP (XCHG EAX,EAX) */
    [0x91] = {1, OP_NONE, 0,0,0,0,0},    /* XCHG ECX,EAX */
    [0x92] = {1, OP_NONE, 0,0,0,0,0},
    [0x93] = {1, OP_NONE, 0,0,0,0,0},
    [0x94] = {1, OP_NONE, 0,0,0,0,0},
    [0x95] = {1, OP_NONE, 0,0,0,0,0},
    [0x96] = {1, OP_NONE, 0,0,0,0,0},
    [0x97] = {1, OP_NONE, 0,0,0,0,0},

    /* 0x98-0x9F */
    [0x98] = {1, OP_NONE, 0,0,0,0,0},    /* CWDE/CDQE */
    [0x99] = {1, OP_NONE, 0,0,0,0,0},    /* CDQ/CQO */
    [0x9A] = {1, OP_NONE, 0,0,0,0,0},    /* CALLF (invalid 64-bit) */
    [0x9B] = {1, OP_NONE, 0,0,0,0,0},    /* FWAIT */
    [0x9C] = {1, OP_NONE, 0,0,0,1,0},    /* PUSHFQ */
    [0x9D] = {1, OP_NONE, 0,0,0,0,1},    /* POPFQ */
    [0x9E] = {1, OP_NONE, 0,0,0,0,0},    /* SAHF */
    [0x9F] = {1, OP_NONE, 0,0,0,0,0},    /* LAHF */

    /* 0xA0-0xAF: MOV/string ops */
    [0xA0] = {1, OP_IMM64, 0,0,0,0,0},   /* MOV AL, moffs8 (uses 8-byte addr in 64-bit) */
    [0xA1] = {1, OP_IMM64, 0,0,0,0,0},   /* MOV EAX, moffs32 */
    [0xA2] = {1, OP_IMM64, 0,0,0,0,0},   /* MOV moffs8, AL */
    [0xA3] = {1, OP_IMM64, 0,0,0,0,0},   /* MOV moffs32, EAX */
    [0xA4] = {1, OP_NONE, 0,0,0,0,0},    /* MOVSB */
    [0xA5] = {1, OP_NONE, 0,0,0,0,0},    /* MOVSD/MOVSQ */
    [0xA6] = {1, OP_NONE, 0,0,0,0,0},    /* CMPSB */
    [0xA7] = {1, OP_NONE, 0,0,0,0,0},    /* CMPSD/CMPSQ */
    [0xA8] = {1, OP_IMM8,  0,0,0,0,0},   /* TEST AL, imm8 */
    [0xA9] = {1, OP_IMM32, 0,0,0,0,0},   /* TEST EAX, imm32 */
    [0xAA] = {1, OP_NONE, 0,0,0,0,0},    /* STOSB */
    [0xAB] = {1, OP_NONE, 0,0,0,0,0},    /* STOSD/STOSQ */
    [0xAC] = {1, OP_NONE, 0,0,0,0,0},    /* LODSB */
    [0xAD] = {1, OP_NONE, 0,0,0,0,0},    /* LODSD/LODSQ */
    [0xAE] = {1, OP_NONE, 0,0,0,0,0},    /* SCASB */
    [0xAF] = {1, OP_NONE, 0,0,0,0,0},    /* SCASD/SCASQ */

    /* 0xB0-0xB7: MOV r8, imm8 */
    [0xB0] = {1, OP_IMM8, 0,0,0,0,0},
    [0xB1] = {1, OP_IMM8, 0,0,0,0,0},
    [0xB2] = {1, OP_IMM8, 0,0,0,0,0},
    [0xB3] = {1, OP_IMM8, 0,0,0,0,0},
    [0xB4] = {1, OP_IMM8, 0,0,0,0,0},
    [0xB5] = {1, OP_IMM8, 0,0,0,0,0},
    [0xB6] = {1, OP_IMM8, 0,0,0,0,0},
    [0xB7] = {1, OP_IMM8, 0,0,0,0,0},

    /* 0xB8-0xBF: MOV r32/r64, imm32/imm64 */
    [0xB8] = {1, OP_IMM32, 0,0,0,0,0},   /* +REX.W = imm64 */
    [0xB9] = {1, OP_IMM32, 0,0,0,0,0},
    [0xBA] = {1, OP_IMM32, 0,0,0,0,0},
    [0xBB] = {1, OP_IMM32, 0,0,0,0,0},
    [0xBC] = {1, OP_IMM32, 0,0,0,0,0},
    [0xBD] = {1, OP_IMM32, 0,0,0,0,0},
    [0xBE] = {1, OP_IMM32, 0,0,0,0,0},
    [0xBF] = {1, OP_IMM32, 0,0,0,0,0},

    /* 0xC0-0xCF */
    [0xC0] = {1, OP_MODRM|OP_IMM8, 0,0,0,0,0},  /* Shift GRP2 r/m8, imm8 */
    [0xC1] = {1, OP_MODRM|OP_IMM8, 0,0,0,0,0},  /* Shift GRP2 r/m32, imm8 */
    [0xC2] = {1, OP_IMM16, 1,1,0,0,0},   /* RET imm16 */
    [0xC3] = {1, OP_NONE,  1,1,0,0,0},   /* RET */
    [0xC4] = {1, OP_MODRM, 0,0,0,0,0},   /* VEX 3-byte / LES (invalid 64-bit) */
    [0xC5] = {1, OP_MODRM, 0,0,0,0,0},   /* VEX 2-byte / LDS (invalid 64-bit) */
    [0xC6] = {1, OP_MODRM|OP_IMM8,  0,0,0,0,0},  /* MOV r/m8, imm8 */
    [0xC7] = {1, OP_MODRM|OP_IMM32, 0,0,0,0,0},  /* MOV r/m32, imm32 */
    [0xC8] = {4, OP_NONE, 0,0,0,0,0},    /* ENTER imm16, imm8 */
    [0xC9] = {1, OP_NONE, 0,0,0,0,0},    /* LEAVE */
    [0xCA] = {1, OP_IMM16, 1,1,0,0,0},   /* RETF imm16 */
    [0xCB] = {1, OP_NONE,  1,1,0,0,0},   /* RETF */
    [0xCC] = {1, OP_NONE,  0,0,0,0,0},   /* INT3 */
    [0xCD] = {1, OP_IMM8,  0,0,0,0,0},   /* INT imm8 */
    [0xCE] = {1, OP_NONE,  0,0,0,0,0},   /* INTO (invalid 64-bit) */
    [0xCF] = {1, OP_NONE,  1,1,0,0,0},   /* IRETQ */

    /* 0xD0-0xDF: Shifts, FPU escape */
    [0xD0] = {1, OP_MODRM, 0,0,0,0,0},   /* Shift GRP2 r/m8, 1 */
    [0xD1] = {1, OP_MODRM, 0,0,0,0,0},   /* Shift GRP2 r/m32, 1 */
    [0xD2] = {1, OP_MODRM, 0,0,0,0,0},   /* Shift GRP2 r/m8, CL */
    [0xD3] = {1, OP_MODRM, 0,0,0,0,0},   /* Shift GRP2 r/m32, CL */
    [0xD4] = {1, OP_IMM8,  0,0,0,0,0},   /* AAM (invalid 64-bit) */
    [0xD5] = {1, OP_IMM8,  0,0,0,0,0},   /* AAD (invalid 64-bit) */
    [0xD6] = {1, OP_NONE,  0,0,0,0,0},   /* SALC (undocumented) */
    [0xD7] = {1, OP_NONE,  0,0,0,0,0},   /* XLAT */
    /* 0xD8-0xDF: x87 FPU */
    [0xD8] = {1, OP_MODRM, 0,0,0,0,0},
    [0xD9] = {1, OP_MODRM, 0,0,0,0,0},
    [0xDA] = {1, OP_MODRM, 0,0,0,0,0},
    [0xDB] = {1, OP_MODRM, 0,0,0,0,0},
    [0xDC] = {1, OP_MODRM, 0,0,0,0,0},
    [0xDD] = {1, OP_MODRM, 0,0,0,0,0},
    [0xDE] = {1, OP_MODRM, 0,0,0,0,0},
    [0xDF] = {1, OP_MODRM, 0,0,0,0,0},

    /* 0xE0-0xEF: Loops, I/O, jumps */
    [0xE0] = {1, OP_REL8, 1,0,0,0,0},    /* LOOPNE */
    [0xE1] = {1, OP_REL8, 1,0,0,0,0},    /* LOOPE */
    [0xE2] = {1, OP_REL8, 1,0,0,0,0},    /* LOOP */
    [0xE3] = {1, OP_REL8, 1,0,0,0,0},    /* JRCXZ */
    [0xE4] = {1, OP_IMM8, 0,0,0,0,0},    /* IN AL, imm8 */
    [0xE5] = {1, OP_IMM8, 0,0,0,0,0},    /* IN EAX, imm8 */
    [0xE6] = {1, OP_IMM8, 0,0,0,0,0},    /* OUT imm8, AL */
    [0xE7] = {1, OP_IMM8, 0,0,0,0,0},    /* OUT imm8, EAX */
    [0xE8] = {1, OP_REL32, 1,0,0,0,0},   /* CALL rel32 */
    [0xE9] = {1, OP_REL32, 1,0,0,0,0},   /* JMP rel32 */
    [0xEA] = {1, OP_NONE,  1,0,0,0,0},   /* JMPF (invalid 64-bit) */
    [0xEB] = {1, OP_REL8,  1,0,0,0,0},   /* JMP rel8 */
    [0xEC] = {1, OP_NONE,  0,0,0,0,0},   /* IN AL, DX */
    [0xED] = {1, OP_NONE,  0,0,0,0,0},   /* IN EAX, DX */
    [0xEE] = {1, OP_NONE,  0,0,0,0,0},   /* OUT DX, AL */
    [0xEF] = {1, OP_NONE,  0,0,0,0,0},   /* OUT DX, EAX */

    /* 0xF0-0xFF: LOCK, REP, HLT, misc */
    [0xF0] = {1, OP_PREFIX, 0,0,0,0,0},  /* LOCK */
    [0xF1] = {1, OP_NONE,  0,0,0,0,0},   /* INT1/ICEBP */
    [0xF2] = {1, OP_PREFIX, 0,0,0,0,0},  /* REPNE/REPNZ */
    [0xF3] = {1, OP_PREFIX, 0,0,0,0,0},  /* REP/REPE/REPZ */
    [0xF4] = {1, OP_NONE,  0,0,0,0,0},   /* HLT */
    [0xF5] = {1, OP_NONE,  0,0,0,0,0},   /* CMC */
    [0xF6] = {1, OP_MODRM, 0,0,0,0,0},   /* GRP3 r/m8 (TEST/NOT/NEG/MUL/DIV) */
    [0xF7] = {1, OP_MODRM, 0,0,0,0,0},   /* GRP3 r/m32 */
    [0xF8] = {1, OP_NONE,  0,0,0,0,0},   /* CLC */
    [0xF9] = {1, OP_NONE,  0,0,0,0,0},   /* STC */
    [0xFA] = {1, OP_NONE,  0,0,0,0,0},   /* CLI */
    [0xFB] = {1, OP_NONE,  0,0,0,0,0},   /* STI */
    [0xFC] = {1, OP_NONE,  0,0,0,0,0},   /* CLD */
    [0xFD] = {1, OP_NONE,  0,0,0,0,0},   /* STD */
    [0xFE] = {1, OP_MODRM, 0,0,0,0,0},   /* GRP4 INC/DEC r/m8 */
    [0xFF] = {1, OP_MODRM, 0,0,0,0,0},   /* GRP5 INC/DEC/CALL/JMP/PUSH r/m32 */
};

/* ── Two-byte Opcode Map (0x0F xx) ───────────────────────────────── */
const opcode_entry_t opcode_table_2byte[256] = {
    /* 0x00-0x0F: System instructions */
    [0x00] = {2, OP_MODRM, 0,0,0,0,0},   /* GRP6 (SLDT/STR/LLDT/LTR/VERR/VERW) */
    [0x01] = {2, OP_MODRM, 0,0,0,0,0},   /* GRP7 (SGDT/SIDT/LGDT/LIDT/SMSW/LMSW/INVLPG) */
    [0x02] = {2, OP_MODRM, 0,0,0,0,0},   /* LAR */
    [0x03] = {2, OP_MODRM, 0,0,0,0,0},   /* LSL */
    [0x05] = {2, OP_NONE,  1,1,0,0,0},   /* SYSCALL */
    [0x06] = {2, OP_NONE,  0,0,0,0,0},   /* CLTS */
    [0x07] = {2, OP_NONE,  1,1,0,0,0},   /* SYSRET */
    [0x08] = {2, OP_NONE,  0,0,0,0,0},   /* INVD */
    [0x09] = {2, OP_NONE,  0,0,0,0,0},   /* WBINVD */
    [0x0B] = {2, OP_NONE,  0,0,0,0,0},   /* UD2 */
    [0x0D] = {2, OP_MODRM, 0,0,0,0,0},   /* PREFETCH */

    /* 0x10-0x1F: SSE move */
    [0x10] = {2, OP_MODRM, 0,0,0,0,0},   /* MOVUPS/MOVSS/MOVUPD/MOVSD */
    [0x11] = {2, OP_MODRM, 0,0,0,0,0},
    [0x12] = {2, OP_MODRM, 0,0,0,0,0},
    [0x13] = {2, OP_MODRM, 0,0,0,0,0},
    [0x14] = {2, OP_MODRM, 0,0,0,0,0},
    [0x15] = {2, OP_MODRM, 0,0,0,0,0},
    [0x16] = {2, OP_MODRM, 0,0,0,0,0},
    [0x17] = {2, OP_MODRM, 0,0,0,0,0},
    [0x18] = {2, OP_MODRM, 0,0,0,0,0},   /* PREFETCH hints */
    [0x1F] = {2, OP_MODRM, 0,0,1,0,0},   /* Multi-byte NOP */

    /* 0x20-0x2F: MOV CR/DR, SSE */
    [0x20] = {2, OP_MODRM, 0,0,0,0,0},   /* MOV r64, CRn */
    [0x21] = {2, OP_MODRM, 0,0,0,0,0},   /* MOV r64, DRn */
    [0x22] = {2, OP_MODRM, 0,0,0,0,0},   /* MOV CRn, r64 */
    [0x23] = {2, OP_MODRM, 0,0,0,0,0},   /* MOV DRn, r64 */
    [0x28] = {2, OP_MODRM, 0,0,0,0,0},   /* MOVAPS */
    [0x29] = {2, OP_MODRM, 0,0,0,0,0},
    [0x2A] = {2, OP_MODRM, 0,0,0,0,0},
    [0x2B] = {2, OP_MODRM, 0,0,0,0,0},
    [0x2C] = {2, OP_MODRM, 0,0,0,0,0},
    [0x2D] = {2, OP_MODRM, 0,0,0,0,0},
    [0x2E] = {2, OP_MODRM, 0,0,0,0,0},
    [0x2F] = {2, OP_MODRM, 0,0,0,0,0},

    /* 0x30-0x3F: MSR, RDTSC, etc. */
    [0x30] = {2, OP_NONE, 0,0,0,0,0},    /* WRMSR */
    [0x31] = {2, OP_NONE, 0,0,0,0,0},    /* RDTSC */
    [0x32] = {2, OP_NONE, 0,0,0,0,0},    /* RDMSR */
    [0x33] = {2, OP_NONE, 0,0,0,0,0},    /* RDPMC */
    [0x34] = {2, OP_NONE, 1,0,0,0,0},    /* SYSENTER */
    [0x35] = {2, OP_NONE, 1,1,0,0,0},    /* SYSEXIT */

    /* 0x40-0x4F: CMOVcc */
    [0x40] = {2, OP_MODRM, 0,0,0,0,0},
    [0x41] = {2, OP_MODRM, 0,0,0,0,0},
    [0x42] = {2, OP_MODRM, 0,0,0,0,0},
    [0x43] = {2, OP_MODRM, 0,0,0,0,0},
    [0x44] = {2, OP_MODRM, 0,0,0,0,0},
    [0x45] = {2, OP_MODRM, 0,0,0,0,0},
    [0x46] = {2, OP_MODRM, 0,0,0,0,0},
    [0x47] = {2, OP_MODRM, 0,0,0,0,0},
    [0x48] = {2, OP_MODRM, 0,0,0,0,0},
    [0x49] = {2, OP_MODRM, 0,0,0,0,0},
    [0x4A] = {2, OP_MODRM, 0,0,0,0,0},
    [0x4B] = {2, OP_MODRM, 0,0,0,0,0},
    [0x4C] = {2, OP_MODRM, 0,0,0,0,0},
    [0x4D] = {2, OP_MODRM, 0,0,0,0,0},
    [0x4E] = {2, OP_MODRM, 0,0,0,0,0},
    [0x4F] = {2, OP_MODRM, 0,0,0,0,0},

    /* 0x50-0x6F: SSE operations */
    [0x50] = {2, OP_MODRM, 0,0,0,0,0},
    [0x51] = {2, OP_MODRM, 0,0,0,0,0},
    [0x52] = {2, OP_MODRM, 0,0,0,0,0},
    [0x53] = {2, OP_MODRM, 0,0,0,0,0},
    [0x54] = {2, OP_MODRM, 0,0,0,0,0},
    [0x55] = {2, OP_MODRM, 0,0,0,0,0},
    [0x56] = {2, OP_MODRM, 0,0,0,0,0},
    [0x57] = {2, OP_MODRM, 0,0,0,0,0},
    [0x58] = {2, OP_MODRM, 0,0,0,0,0},
    [0x59] = {2, OP_MODRM, 0,0,0,0,0},
    [0x5A] = {2, OP_MODRM, 0,0,0,0,0},
    [0x5B] = {2, OP_MODRM, 0,0,0,0,0},
    [0x5C] = {2, OP_MODRM, 0,0,0,0,0},
    [0x5D] = {2, OP_MODRM, 0,0,0,0,0},
    [0x5E] = {2, OP_MODRM, 0,0,0,0,0},
    [0x5F] = {2, OP_MODRM, 0,0,0,0,0},
    [0x60] = {2, OP_MODRM, 0,0,0,0,0},
    [0x61] = {2, OP_MODRM, 0,0,0,0,0},
    [0x62] = {2, OP_MODRM, 0,0,0,0,0},
    [0x63] = {2, OP_MODRM, 0,0,0,0,0},
    [0x64] = {2, OP_MODRM, 0,0,0,0,0},
    [0x65] = {2, OP_MODRM, 0,0,0,0,0},
    [0x66] = {2, OP_MODRM, 0,0,0,0,0},
    [0x67] = {2, OP_MODRM, 0,0,0,0,0},
    [0x68] = {2, OP_MODRM, 0,0,0,0,0},
    [0x69] = {2, OP_MODRM, 0,0,0,0,0},
    [0x6A] = {2, OP_MODRM, 0,0,0,0,0},
    [0x6B] = {2, OP_MODRM, 0,0,0,0,0},
    [0x6C] = {2, OP_MODRM, 0,0,0,0,0},
    [0x6D] = {2, OP_MODRM, 0,0,0,0,0},
    [0x6E] = {2, OP_MODRM, 0,0,0,0,0},
    [0x6F] = {2, OP_MODRM, 0,0,0,0,0},

    /* 0x70-0x7F: SSE + Jcc rel32 */
    [0x70] = {2, OP_MODRM|OP_IMM8, 0,0,0,0,0},
    [0x71] = {2, OP_MODRM|OP_IMM8, 0,0,0,0,0},
    [0x72] = {2, OP_MODRM|OP_IMM8, 0,0,0,0,0},
    [0x73] = {2, OP_MODRM|OP_IMM8, 0,0,0,0,0},
    [0x74] = {2, OP_MODRM, 0,0,0,0,0},
    [0x75] = {2, OP_MODRM, 0,0,0,0,0},
    [0x76] = {2, OP_MODRM, 0,0,0,0,0},
    [0x77] = {2, OP_NONE,  0,0,0,0,0},   /* EMMS */
    [0x7E] = {2, OP_MODRM, 0,0,0,0,0},
    [0x7F] = {2, OP_MODRM, 0,0,0,0,0},

    /* 0x80-0x8F: Jcc rel32 (long conditional jumps) */
    [0x80] = {2, OP_REL32, 1,0,0,0,0},   /* JO rel32 */
    [0x81] = {2, OP_REL32, 1,0,0,0,0},
    [0x82] = {2, OP_REL32, 1,0,0,0,0},
    [0x83] = {2, OP_REL32, 1,0,0,0,0},
    [0x84] = {2, OP_REL32, 1,0,0,0,0},
    [0x85] = {2, OP_REL32, 1,0,0,0,0},
    [0x86] = {2, OP_REL32, 1,0,0,0,0},
    [0x87] = {2, OP_REL32, 1,0,0,0,0},
    [0x88] = {2, OP_REL32, 1,0,0,0,0},
    [0x89] = {2, OP_REL32, 1,0,0,0,0},
    [0x8A] = {2, OP_REL32, 1,0,0,0,0},
    [0x8B] = {2, OP_REL32, 1,0,0,0,0},
    [0x8C] = {2, OP_REL32, 1,0,0,0,0},
    [0x8D] = {2, OP_REL32, 1,0,0,0,0},
    [0x8E] = {2, OP_REL32, 1,0,0,0,0},
    [0x8F] = {2, OP_REL32, 1,0,0,0,0},

    /* 0x90-0x9F: SETcc */
    [0x90] = {2, OP_MODRM, 0,0,0,0,0},
    [0x91] = {2, OP_MODRM, 0,0,0,0,0},
    [0x92] = {2, OP_MODRM, 0,0,0,0,0},
    [0x93] = {2, OP_MODRM, 0,0,0,0,0},
    [0x94] = {2, OP_MODRM, 0,0,0,0,0},
    [0x95] = {2, OP_MODRM, 0,0,0,0,0},
    [0x96] = {2, OP_MODRM, 0,0,0,0,0},
    [0x97] = {2, OP_MODRM, 0,0,0,0,0},
    [0x98] = {2, OP_MODRM, 0,0,0,0,0},
    [0x99] = {2, OP_MODRM, 0,0,0,0,0},
    [0x9A] = {2, OP_MODRM, 0,0,0,0,0},
    [0x9B] = {2, OP_MODRM, 0,0,0,0,0},
    [0x9C] = {2, OP_MODRM, 0,0,0,0,0},
    [0x9D] = {2, OP_MODRM, 0,0,0,0,0},
    [0x9E] = {2, OP_MODRM, 0,0,0,0,0},
    [0x9F] = {2, OP_MODRM, 0,0,0,0,0},

    /* 0xA0-0xAF: misc */
    [0xA0] = {2, OP_NONE, 0,0,0,1,0},    /* PUSH FS */
    [0xA1] = {2, OP_NONE, 0,0,0,0,1},    /* POP FS */
    [0xA2] = {2, OP_NONE, 0,0,0,0,0},    /* CPUID */
    [0xA3] = {2, OP_MODRM, 0,0,0,0,0},   /* BT */
    [0xA4] = {2, OP_MODRM|OP_IMM8, 0,0,0,0,0}, /* SHLD imm8 */
    [0xA5] = {2, OP_MODRM, 0,0,0,0,0},   /* SHLD CL */
    [0xA8] = {2, OP_NONE, 0,0,0,1,0},    /* PUSH GS */
    [0xA9] = {2, OP_NONE, 0,0,0,0,1},    /* POP GS */
    [0xAB] = {2, OP_MODRM, 0,0,0,0,0},   /* BTS */
    [0xAC] = {2, OP_MODRM|OP_IMM8, 0,0,0,0,0}, /* SHRD imm8 */
    [0xAD] = {2, OP_MODRM, 0,0,0,0,0},   /* SHRD CL */
    [0xAE] = {2, OP_MODRM, 0,0,0,0,0},   /* FXSAVE/FXRSTOR/LDMXCSR/STMXCSR/MFENCE/LFENCE/SFENCE */
    [0xAF] = {2, OP_MODRM, 0,0,0,0,0},   /* IMUL */

    /* 0xB0-0xBF */
    [0xB0] = {2, OP_MODRM, 0,0,0,0,0},   /* CMPXCHG r/m8 */
    [0xB1] = {2, OP_MODRM, 0,0,0,0,0},   /* CMPXCHG r/m32 */
    [0xB3] = {2, OP_MODRM, 0,0,0,0,0},   /* BTR */
    [0xB6] = {2, OP_MODRM, 0,0,0,0,0},   /* MOVZX r32, r/m8 */
    [0xB7] = {2, OP_MODRM, 0,0,0,0,0},   /* MOVZX r32, r/m16 */
    [0xBA] = {2, OP_MODRM|OP_IMM8, 0,0,0,0,0}, /* BT/BTS/BTR/BTC imm8 */
    [0xBB] = {2, OP_MODRM, 0,0,0,0,0},   /* BTC */
    [0xBC] = {2, OP_MODRM, 0,0,0,0,0},   /* BSF */
    [0xBD] = {2, OP_MODRM, 0,0,0,0,0},   /* BSR */
    [0xBE] = {2, OP_MODRM, 0,0,0,0,0},   /* MOVSX r32, r/m8 */
    [0xBF] = {2, OP_MODRM, 0,0,0,0,0},   /* MOVSX r32, r/m16 */

    /* 0xC0-0xCF */
    [0xC0] = {2, OP_MODRM, 0,0,0,0,0},   /* XADD r/m8 */
    [0xC1] = {2, OP_MODRM, 0,0,0,0,0},   /* XADD r/m32 */
    [0xC2] = {2, OP_MODRM|OP_IMM8, 0,0,0,0,0}, /* CMPxxPS/PD */
    [0xC3] = {2, OP_MODRM, 0,0,0,0,0},   /* MOVNTI */
    [0xC4] = {2, OP_MODRM|OP_IMM8, 0,0,0,0,0}, /* PINSRW */
    [0xC5] = {2, OP_MODRM|OP_IMM8, 0,0,0,0,0}, /* PEXTRW */
    [0xC6] = {2, OP_MODRM|OP_IMM8, 0,0,0,0,0}, /* SHUFPS/PD */
    [0xC7] = {2, OP_MODRM, 0,0,0,0,0},   /* CMPXCHG8B/16B */
    [0xC8] = {2, OP_NONE, 0,0,0,0,0},    /* BSWAP EAX */
    [0xC9] = {2, OP_NONE, 0,0,0,0,0},    /* BSWAP ECX */
    [0xCA] = {2, OP_NONE, 0,0,0,0,0},
    [0xCB] = {2, OP_NONE, 0,0,0,0,0},
    [0xCC] = {2, OP_NONE, 0,0,0,0,0},
    [0xCD] = {2, OP_NONE, 0,0,0,0,0},
    [0xCE] = {2, OP_NONE, 0,0,0,0,0},
    [0xCF] = {2, OP_NONE, 0,0,0,0,0},

    /* 0xD0-0xFF: SSE2/MMX operations */
    [0xD0] = {2, OP_MODRM, 0,0,0,0,0},
    [0xD1] = {2, OP_MODRM, 0,0,0,0,0},
    [0xD2] = {2, OP_MODRM, 0,0,0,0,0},
    [0xD3] = {2, OP_MODRM, 0,0,0,0,0},
    [0xD4] = {2, OP_MODRM, 0,0,0,0,0},
    [0xD5] = {2, OP_MODRM, 0,0,0,0,0},
    [0xD6] = {2, OP_MODRM, 0,0,0,0,0},
    [0xD7] = {2, OP_MODRM, 0,0,0,0,0},
    [0xD8] = {2, OP_MODRM, 0,0,0,0,0},
    [0xD9] = {2, OP_MODRM, 0,0,0,0,0},
    [0xDA] = {2, OP_MODRM, 0,0,0,0,0},
    [0xDB] = {2, OP_MODRM, 0,0,0,0,0},
    [0xDC] = {2, OP_MODRM, 0,0,0,0,0},
    [0xDD] = {2, OP_MODRM, 0,0,0,0,0},
    [0xDE] = {2, OP_MODRM, 0,0,0,0,0},
    [0xDF] = {2, OP_MODRM, 0,0,0,0,0},
    [0xE0] = {2, OP_MODRM, 0,0,0,0,0},
    [0xE1] = {2, OP_MODRM, 0,0,0,0,0},
    [0xE2] = {2, OP_MODRM, 0,0,0,0,0},
    [0xE3] = {2, OP_MODRM, 0,0,0,0,0},
    [0xE4] = {2, OP_MODRM, 0,0,0,0,0},
    [0xE5] = {2, OP_MODRM, 0,0,0,0,0},
    [0xE6] = {2, OP_MODRM, 0,0,0,0,0},
    [0xE7] = {2, OP_MODRM, 0,0,0,0,0},
    [0xE8] = {2, OP_MODRM, 0,0,0,0,0},
    [0xE9] = {2, OP_MODRM, 0,0,0,0,0},
    [0xEA] = {2, OP_MODRM, 0,0,0,0,0},
    [0xEB] = {2, OP_MODRM, 0,0,0,0,0},
    [0xEC] = {2, OP_MODRM, 0,0,0,0,0},
    [0xED] = {2, OP_MODRM, 0,0,0,0,0},
    [0xEE] = {2, OP_MODRM, 0,0,0,0,0},
    [0xEF] = {2, OP_MODRM, 0,0,0,0,0},
    [0xF0] = {2, OP_MODRM, 0,0,0,0,0},
    [0xF1] = {2, OP_MODRM, 0,0,0,0,0},
    [0xF2] = {2, OP_MODRM, 0,0,0,0,0},
    [0xF3] = {2, OP_MODRM, 0,0,0,0,0},
    [0xF4] = {2, OP_MODRM, 0,0,0,0,0},
    [0xF5] = {2, OP_MODRM, 0,0,0,0,0},
    [0xF6] = {2, OP_MODRM, 0,0,0,0,0},
    [0xF7] = {2, OP_MODRM, 0,0,0,0,0},
    [0xF8] = {2, OP_MODRM, 0,0,0,0,0},
    [0xF9] = {2, OP_MODRM, 0,0,0,0,0},
    [0xFA] = {2, OP_MODRM, 0,0,0,0,0},
    [0xFB] = {2, OP_MODRM, 0,0,0,0,0},
    [0xFC] = {2, OP_MODRM, 0,0,0,0,0},
    [0xFD] = {2, OP_MODRM, 0,0,0,0,0},
    [0xFE] = {2, OP_MODRM, 0,0,0,0,0},
    [0xFF] = {2, OP_NONE,  0,0,0,0,0},   /* UD0 */
};

/* Calculate extra bytes from ModR/M encoding (SIB + displacement) */
int modrm_extra_bytes(uint8_t modrm, int addr_size_override) {
    uint8_t mod = (modrm >> 6) & 3;
    uint8_t rm  = modrm & 7;
    int extra = 0;

    if (mod == 3) {
        /* Register-direct: no extra bytes */
        return 0;
    }

    /* Check for SIB byte (rm == 4 in 64-bit mode, except mod == 3) */
    if (rm == 4) {
        extra += 1;  /* SIB byte */
    }

    switch (mod) {
    case 0:
        if (rm == 5) {
            /* RIP-relative in 64-bit mode: 4-byte displacement */
            extra += 4;
        }
        break;
    case 1:
        extra += 1;  /* 8-bit displacement */
        break;
    case 2:
        extra += 4;  /* 32-bit displacement */
        break;
    }

    /* SIB with base == 5 and mod == 0 means disp32 (no base register) */
    if (rm == 4 && mod == 0) {
        /* Need to peek at SIB to check base field — handled in decoder */
    }

    return extra;
}
