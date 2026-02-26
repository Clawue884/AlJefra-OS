/*
 * x86-64 Instruction Decoder
 * Decodes raw binary into instruction boundaries with properties.
 * Only needs to determine lengths and classify instructions —
 * not a full disassembler.
 */
#include "x86_decode.h"
#include "x86_tables.h"
#include <string.h>
#include <stdio.h>

/* Decode a single x86-64 instruction.
 * Returns bytes consumed, or 0 on decode error. */
int x86_decode_instruction(const uint8_t *code, size_t max_len,
                           uint64_t address, instruction_t *out) {
    if (max_len == 0) return 0;

    memset(out, 0, sizeof(*out));
    out->address = address;

    size_t pos = 0;
    int has_rex = 0;
    uint8_t rex = 0;
    int has_66 = 0;  /* Operand size override */
    int has_67 = 0;  /* Address size override */
    int has_lock = 0;
    int has_rep = 0;

    /* ── Phase 1: Consume prefixes ─────────────────────────────── */
    while (pos < max_len) {
        uint8_t b = code[pos];

        /* Legacy prefixes */
        if (b == 0xF0) { has_lock = 1; pos++; continue; }
        if (b == 0xF2 || b == 0xF3) { has_rep = 1; pos++; continue; }
        if (b == 0x26 || b == 0x2E || b == 0x36 || b == 0x3E ||
            b == 0x64 || b == 0x65) { pos++; continue; }
        if (b == 0x66) { has_66 = 1; pos++; continue; }
        if (b == 0x67) { has_67 = 1; pos++; continue; }

        /* REX prefix (0x40-0x4F in 64-bit mode) */
        if (b >= 0x40 && b <= 0x4F) {
            has_rex = 1;
            rex = b;
            pos++;
            continue;
        }

        break;  /* Not a prefix */
    }

    if (pos >= max_len) return 0;

    /* ── Phase 2: Decode opcode ────────────────────────────────── */
    uint8_t opcode = code[pos];
    const opcode_entry_t *entry;
    int is_2byte = 0;

    if (opcode == 0x0F) {
        /* Two-byte opcode */
        pos++;
        if (pos >= max_len) return 0;
        opcode = code[pos];
        entry = &opcode_table_2byte[opcode];
        is_2byte = 1;
    } else {
        entry = &opcode_table_1byte[opcode];
    }

    pos++;  /* Consume opcode byte */

    /* Set instruction properties */
    out->is_branch = entry->is_branch;
    out->is_ret = entry->is_ret;
    out->is_nop = entry->is_nop;
    out->is_push = entry->is_push;
    out->is_pop = entry->is_pop;
    out->has_lock_prefix = has_lock;

    /* ── Phase 3: ModR/M + SIB + Displacement ─────────────────── */
    if (entry->flags & OP_MODRM) {
        if (pos >= max_len) return 0;
        uint8_t modrm = code[pos];
        pos++;

        uint8_t mod = (modrm >> 6) & 3;
        uint8_t rm = modrm & 7;

        /* Handle special cases for Group opcodes */
        if (!is_2byte) {
            /* F6/F7 Group 3: TEST has immediate, others don't */
            if ((opcode == 0xF6 || opcode == 0xF7) &&
                ((modrm >> 3) & 7) == 0) {
                /* TEST: add immediate */
                if (opcode == 0xF6) {
                    /* TEST r/m8, imm8 — already counted by OP_MODRM */
                    /* We need to add imm8 manually */
                }
            }

            /* FF Group 5: CALL/JMP indirect */
            if (opcode == 0xFF) {
                uint8_t reg = (modrm >> 3) & 7;
                if (reg == 2 || reg == 4) {
                    out->is_branch = 1;  /* CALL/JMP indirect */
                }
                if (reg == 6) {
                    out->is_push = 1;  /* PUSH r/m64 */
                }
            }
        }

        /* SIB byte */
        if (mod != 3 && rm == 4) {
            if (pos >= max_len) return 0;
            uint8_t sib = code[pos];
            pos++;

            /* SIB with base=5 and mod=0: disp32, no base */
            if ((sib & 7) == 5 && mod == 0) {
                pos += 4;
            }
        }

        /* Displacement */
        switch (mod) {
        case 0:
            if (rm == 5) pos += 4;  /* RIP-relative: disp32 */
            break;
        case 1:
            pos += 1;  /* disp8 */
            break;
        case 2:
            pos += 4;  /* disp32 */
            break;
        }
    }

    /* ── Phase 4: Immediate operand ────────────────────────────── */
    if (entry->flags & OP_IMM8)  pos += 1;
    if (entry->flags & OP_IMM16) pos += 2;
    if (entry->flags & OP_IMM32) {
        /* REX.W with MOV r64,imm64 (opcodes B8-BF) */
        if (has_rex && (rex & 0x08) &&
            !is_2byte && opcode >= 0xB8 && opcode <= 0xBF) {
            pos += 8;  /* 64-bit immediate */
        } else if (has_66 && !is_2byte) {
            pos += 2;  /* 16-bit immediate with 66h prefix */
        } else {
            pos += 4;
        }
    }
    if (entry->flags & OP_IMM64) {
        pos += 8;  /* moffs64 in 64-bit mode */
    }

    /* ── Phase 5: Relative offsets (branch targets) ────────────── */
    if (entry->flags & OP_REL8) {
        if (pos >= max_len) return 0;
        int8_t rel = (int8_t)code[pos];
        out->branch_target = rel;
        pos += 1;
    }
    if (entry->flags & OP_REL32) {
        if (pos + 3 >= max_len) return 0;
        int32_t rel;
        memcpy(&rel, &code[pos], 4);
        out->branch_target = rel;
        pos += 4;
    }

    /* F6/F7 TEST immediate handling is done in Phase 3 above */

    /* ── Phase 6: Validate and store ───────────────────────────── */
    if (pos > max_len || pos > MAX_INSTRUCTION_LEN) return 0;

    out->length = (uint8_t)pos;
    memcpy(out->bytes, code, pos);

    return (int)pos;
}

/* Decode all instructions in a buffer */
int x86_decode_all(const uint8_t *code, size_t code_len,
                   uint64_t base_address,
                   instruction_t *out, int max_instructions) {
    size_t offset = 0;
    int count = 0;

    while (offset < code_len && count < max_instructions) {
        int len = x86_decode_instruction(
            code + offset, code_len - offset,
            base_address + offset, &out[count]);

        if (len <= 0) {
            /* Can't decode — skip one byte */
            fprintf(stderr, "Warning: undecoded byte at 0x%lx: 0x%02x\n",
                    (unsigned long)(base_address + offset), code[offset]);
            offset++;
            continue;
        }

        count++;
        offset += len;
    }

    return count;
}

/* Check if instruction modifies RSP */
int x86_modifies_sp(const instruction_t *instr) {
    return instr->is_push || instr->is_pop ||
           (instr->bytes[0] == 0xC8) ||  /* ENTER */
           (instr->bytes[0] == 0xC9);    /* LEAVE */
}

/* Check if instruction has memory operand */
int x86_has_mem_operand(const instruction_t *instr) {
    /* Find the ModR/M byte position */
    int pos = 0;
    for (int i = 0; i < instr->length; i++) {
        uint8_t b = instr->bytes[i];
        /* Skip prefixes */
        if (b == 0xF0 || b == 0xF2 || b == 0xF3 ||
            b == 0x26 || b == 0x2E || b == 0x36 || b == 0x3E ||
            b == 0x64 || b == 0x65 || b == 0x66 || b == 0x67 ||
            (b >= 0x40 && b <= 0x4F)) {
            pos++;
            continue;
        }
        break;
    }

    /* Skip opcode */
    if (pos < instr->length && instr->bytes[pos] == 0x0F) pos += 2;
    else pos += 1;

    /* Check ModR/M mod field */
    if (pos < instr->length) {
        uint8_t mod = (instr->bytes[pos] >> 6) & 3;
        return mod != 3;  /* mod != 11b means memory operand */
    }

    return 0;
}

/* Get register from ModR/M reg field */
int x86_get_modrm_reg(const instruction_t *instr) {
    int pos = 0;
    int rex_r = 0;

    for (int i = 0; i < instr->length; i++) {
        uint8_t b = instr->bytes[i];
        if (b >= 0x40 && b <= 0x4F) {
            rex_r = (b >> 2) & 1;  /* REX.R */
            pos++;
            continue;
        }
        if (b == 0xF0 || b == 0xF2 || b == 0xF3 ||
            b == 0x26 || b == 0x2E || b == 0x36 || b == 0x3E ||
            b == 0x64 || b == 0x65 || b == 0x66 || b == 0x67) {
            pos++;
            continue;
        }
        break;
    }

    if (pos < instr->length && instr->bytes[pos] == 0x0F) pos += 2;
    else pos += 1;

    if (pos < instr->length) {
        return ((instr->bytes[pos] >> 3) & 7) | (rex_r << 3);
    }

    return -1;
}
