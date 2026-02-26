/*
 * Safe Instruction Substitution Patterns
 * Based on Agner Fog's optimization manuals and Intel optimization guides.
 * Each substitution is semantically equivalent but may be faster.
 */
#include "patterns.h"
#include <string.h>
#include <stddef.h>

/* ── Pattern byte arrays ─────────────────────────────────────────── */

/* 1. XOR reg,reg (2B) instead of MOV reg,0 (5B) — smaller, breaks dep chain */
static const uint8_t p01_from[] = {0xB8, 0x00, 0x00, 0x00, 0x00}; /* mov eax, 0 */
static const uint8_t p01_to[]   = {0x31, 0xC0};                    /* xor eax, eax */

static const uint8_t p02_from[] = {0xBB, 0x00, 0x00, 0x00, 0x00}; /* mov ebx, 0 */
static const uint8_t p02_to[]   = {0x31, 0xDB};                    /* xor ebx, ebx */

static const uint8_t p03_from[] = {0xB9, 0x00, 0x00, 0x00, 0x00}; /* mov ecx, 0 */
static const uint8_t p03_to[]   = {0x31, 0xC9};                    /* xor ecx, ecx */

static const uint8_t p04_from[] = {0xBA, 0x00, 0x00, 0x00, 0x00}; /* mov edx, 0 */
static const uint8_t p04_to[]   = {0x31, 0xD2};                    /* xor edx, edx */

/* 5. TEST reg,reg instead of CMP reg,0 — smaller encoding */
static const uint8_t p05_from[] = {0x3C, 0x00};                    /* cmp al, 0 */
static const uint8_t p05_to[]   = {0x84, 0xC0};                    /* test al, al */

static const uint8_t p06_from[] = {0x83, 0xF8, 0x00};              /* cmp eax, 0 */
static const uint8_t p06_to[]   = {0x85, 0xC0};                    /* test eax, eax */

static const uint8_t p07_from[] = {0x83, 0xF9, 0x00};              /* cmp ecx, 0 */
static const uint8_t p07_to[]   = {0x85, 0xC9};                    /* test ecx, ecx */

static const uint8_t p08_from[] = {0x83, 0xFA, 0x00};              /* cmp edx, 0 */
static const uint8_t p08_to[]   = {0x85, 0xD2};                    /* test edx, edx */

static const uint8_t p09_from[] = {0x83, 0xFB, 0x00};              /* cmp ebx, 0 */
static const uint8_t p09_to[]   = {0x85, 0xDB};                    /* test ebx, ebx */

/* 10. LEA for add — can use different port */
static const uint8_t p10_from[] = {0x48, 0x83, 0xC0, 0x01};       /* add rax, 1 */
static const uint8_t p10_to[]   = {0x48, 0xFF, 0xC0, 0x90};       /* inc rax + nop */

static const uint8_t p11_from[] = {0x48, 0x83, 0xE8, 0x01};       /* sub rax, 1 */
static const uint8_t p11_to[]   = {0x48, 0xFF, 0xC8, 0x90};       /* dec rax + nop */

/* 12. INC/DEC vs ADD/SUB 1 — INC doesn't modify CF (partial flag stall on older CPUs) */
/* On modern CPUs (Skylake+), INC is fine and saves a byte */
static const uint8_t p12_from[] = {0x83, 0xC0, 0x01};              /* add eax, 1 */
static const uint8_t p12_to[]   = {0xFF, 0xC0, 0x90};              /* inc eax + nop */

static const uint8_t p13_from[] = {0x83, 0xE8, 0x01};              /* sub eax, 1 */
static const uint8_t p13_to[]   = {0xFF, 0xC8, 0x90};              /* dec eax + nop */

/* 14. SUB reg,reg instead of XOR reg,reg (both zero, but XOR preferred) */
static const uint8_t p14_from[] = {0x29, 0xC0};                    /* sub eax, eax */
static const uint8_t p14_to[]   = {0x31, 0xC0};                    /* xor eax, eax */

/* 15. MOV reg,reg via LEA (can use AGU port) */
static const uint8_t p15_from[] = {0x48, 0x89, 0xC1};              /* mov rcx, rax */
static const uint8_t p15_to[]   = {0x48, 0x8D, 0x08};              /* lea rcx, [rax] — NOTE: only valid if rax not mem */

/* 16-18. NOP patterns — can be eliminated */
static const uint8_t p16_from[] = {0x90};                          /* nop */
static const uint8_t p16_to[]   = {0x90};                          /* stays nop (for alignment analysis) */

static const uint8_t p17_from[] = {0x66, 0x90};                    /* 2-byte nop */
static const uint8_t p17_to[]   = {0x66, 0x90};

static const uint8_t p18_from[] = {0x0F, 0x1F, 0x00};              /* 3-byte nop */
static const uint8_t p18_to[]   = {0x0F, 0x1F, 0x00};

/* 19. AND reg,-1 is a nop (clears OF/CF, sets SF/ZF/PF) */
static const uint8_t p19_from[] = {0x83, 0xE0, 0xFF};              /* and eax, -1 */
static const uint8_t p19_to[]   = {0x85, 0xC0, 0x90};              /* test eax,eax + nop */

/* 20. OR reg,0 is equivalent to TEST reg,reg for flag effects */
static const uint8_t p20_from[] = {0x83, 0xC8, 0x00};              /* or eax, 0 */
static const uint8_t p20_to[]   = {0x85, 0xC0, 0x90};              /* test eax,eax + nop */

/* 21-24. REX.W versions of zero-register patterns */
static const uint8_t p21_from[] = {0x48, 0xC7, 0xC0, 0x00, 0x00, 0x00, 0x00}; /* mov rax, 0 */
static const uint8_t p21_to[]   = {0x48, 0x31, 0xC0, 0x90, 0x90, 0x90, 0x90}; /* xor rax,rax + 4 nops */

static const uint8_t p22_from[] = {0x48, 0xC7, 0xC1, 0x00, 0x00, 0x00, 0x00}; /* mov rcx, 0 */
static const uint8_t p22_to[]   = {0x48, 0x31, 0xC9, 0x90, 0x90, 0x90, 0x90}; /* xor rcx,rcx + nops */

static const uint8_t p23_from[] = {0x48, 0xC7, 0xC2, 0x00, 0x00, 0x00, 0x00}; /* mov rdx, 0 */
static const uint8_t p23_to[]   = {0x48, 0x31, 0xD2, 0x90, 0x90, 0x90, 0x90}; /* xor rdx,rdx + nops */

static const uint8_t p24_from[] = {0x48, 0xC7, 0xC3, 0x00, 0x00, 0x00, 0x00}; /* mov rbx, 0 */
static const uint8_t p24_to[]   = {0x48, 0x31, 0xDB, 0x90, 0x90, 0x90, 0x90}; /* xor rbx,rbx + nops */

/* 25-28. SHL/SHR by 1 vs ADD reg,reg / shift optimization */
static const uint8_t p25_from[] = {0xD1, 0xE0};                    /* shl eax, 1 */
static const uint8_t p25_to[]   = {0x01, 0xC0};                    /* add eax, eax */

static const uint8_t p26_from[] = {0xD1, 0xE1};                    /* shl ecx, 1 */
static const uint8_t p26_to[]   = {0x01, 0xC9};                    /* add ecx, ecx */

/* 27. MUL by power of 2 → shift */
static const uint8_t p27_from[] = {0x6B, 0xC0, 0x02};              /* imul eax, eax, 2 */
static const uint8_t p27_to[]   = {0x01, 0xC0, 0x90};              /* add eax, eax + nop */

static const uint8_t p28_from[] = {0x6B, 0xC0, 0x04};              /* imul eax, eax, 4 */
static const uint8_t p28_to[]   = {0xC1, 0xE0, 0x02};              /* shl eax, 2 */

/* 29-30. MOVZX elimination */
static const uint8_t p29_from[] = {0x0F, 0xB6, 0xC0};              /* movzx eax, al */
static const uint8_t p29_to[]   = {0x0F, 0xB6, 0xC0};              /* keep (CPU handles dep break) */

/* 31-34. CMP+Jcc → TEST+Jcc optimization */
static const uint8_t p31_from[] = {0x48, 0x83, 0xF8, 0x00};        /* cmp rax, 0 */
static const uint8_t p31_to[]   = {0x48, 0x85, 0xC0, 0x90};        /* test rax, rax + nop */

static const uint8_t p32_from[] = {0x48, 0x83, 0xF9, 0x00};        /* cmp rcx, 0 */
static const uint8_t p32_to[]   = {0x48, 0x85, 0xC9, 0x90};        /* test rcx, rcx + nop */

static const uint8_t p33_from[] = {0x48, 0x83, 0xFA, 0x00};        /* cmp rdx, 0 */
static const uint8_t p33_to[]   = {0x48, 0x85, 0xD2, 0x90};        /* test rdx, rdx + nop */

static const uint8_t p34_from[] = {0x48, 0x83, 0xFB, 0x00};        /* cmp rbx, 0 */
static const uint8_t p34_to[]   = {0x48, 0x85, 0xDB, 0x90};        /* test rbx, rbx + nop */

/* 35-38. PUSH/POP with LEA RSP adjustments */
/* These are risky — only applied when stack balance is verified */

/* 39-42. Alignment: replace short NOPs with long NOPs */
static const uint8_t p39_from[] = {0x90, 0x90};                    /* 2x nop */
static const uint8_t p39_to[]   = {0x66, 0x90};                    /* 2-byte nop */

static const uint8_t p40_from[] = {0x90, 0x90, 0x90};              /* 3x nop */
static const uint8_t p40_to[]   = {0x0F, 0x1F, 0x00};              /* 3-byte nop */

static const uint8_t p41_from[] = {0x90, 0x90, 0x90, 0x90};        /* 4x nop */
static const uint8_t p41_to[]   = {0x0F, 0x1F, 0x40, 0x00};        /* 4-byte nop */

static const uint8_t p42_from[] = {0x90, 0x90, 0x90, 0x90, 0x90};  /* 5x nop */
static const uint8_t p42_to[]   = {0x0F, 0x1F, 0x44, 0x00, 0x00};  /* 5-byte nop */

/* 43-46. LOCK CMPXCHG optimizations — MUST preserve LOCK */
/* These are only applied with extreme care */

/* 47. STOSB loop → REP STOSB (when provably safe) */
/* 48. MOVSB loop → REP MOVSB */
/* These require loop detection — applied in mutator.c */

/* ── Substitution table ──────────────────────────────────────────── */
static const substitution_t substitutions[] = {
    {p01_from, 5, p01_to, 2, "mov eax,0 -> xor eax,eax", -3},
    {p02_from, 5, p02_to, 2, "mov ebx,0 -> xor ebx,ebx", -3},
    {p03_from, 5, p03_to, 2, "mov ecx,0 -> xor ecx,ecx", -3},
    {p04_from, 5, p04_to, 2, "mov edx,0 -> xor edx,edx", -3},
    {p05_from, 2, p05_to, 2, "cmp al,0 -> test al,al",    0},
    {p06_from, 3, p06_to, 2, "cmp eax,0 -> test eax,eax",-1},
    {p07_from, 3, p07_to, 2, "cmp ecx,0 -> test ecx,ecx",-1},
    {p08_from, 3, p08_to, 2, "cmp edx,0 -> test edx,edx",-1},
    {p09_from, 3, p09_to, 2, "cmp ebx,0 -> test ebx,ebx",-1},
    {p10_from, 4, p10_to, 4, "add rax,1 -> inc rax+nop",  0},
    {p11_from, 4, p11_to, 4, "sub rax,1 -> dec rax+nop",  0},
    {p12_from, 3, p12_to, 3, "add eax,1 -> inc eax+nop",  0},
    {p13_from, 3, p13_to, 3, "sub eax,1 -> dec eax+nop",  0},
    {p14_from, 2, p14_to, 2, "sub eax,eax -> xor eax,eax",0},
    {p19_from, 3, p19_to, 3, "and eax,-1 -> test+nop",    0},
    {p20_from, 3, p20_to, 3, "or eax,0 -> test+nop",      0},
    {p21_from, 7, p21_to, 7, "mov rax,0 -> xor rax,rax",  0},
    {p22_from, 7, p22_to, 7, "mov rcx,0 -> xor rcx,rcx",  0},
    {p23_from, 7, p23_to, 7, "mov rdx,0 -> xor rdx,rdx",  0},
    {p24_from, 7, p24_to, 7, "mov rbx,0 -> xor rbx,rbx",  0},
    {p25_from, 2, p25_to, 2, "shl eax,1 -> add eax,eax",  0},
    {p26_from, 2, p26_to, 2, "shl ecx,1 -> add ecx,ecx",  0},
    {p27_from, 3, p27_to, 3, "imul eax,2 -> add+nop",     0},
    {p28_from, 3, p28_to, 3, "imul eax,4 -> shl eax,2",   0},
    {p31_from, 4, p31_to, 4, "cmp rax,0 -> test rax,rax", 0},
    {p32_from, 4, p32_to, 4, "cmp rcx,0 -> test rcx,rcx", 0},
    {p33_from, 4, p33_to, 4, "cmp rdx,0 -> test rdx,rdx", 0},
    {p34_from, 4, p34_to, 4, "cmp rbx,0 -> test rbx,rbx", 0},
    {p39_from, 2, p39_to, 2, "2x nop -> 2-byte nop",      0},
    {p40_from, 3, p40_to, 3, "3x nop -> 3-byte nop",      0},
    {p41_from, 4, p41_to, 4, "4x nop -> 4-byte nop",      0},
    {p42_from, 5, p42_to, 5, "5x nop -> 5-byte nop",      0},
};

#define NUM_SUBSTITUTIONS (sizeof(substitutions) / sizeof(substitutions[0]))

int patterns_get_substitutions(const substitution_t **out) {
    *out = substitutions;
    return (int)NUM_SUBSTITUTIONS;
}

int patterns_find_match(const uint8_t *code, int code_len, int offset) {
    for (int i = 0; i < (int)NUM_SUBSTITUTIONS; i++) {
        if (offset + substitutions[i].from_len <= code_len) {
            if (memcmp(code + offset, substitutions[i].from_bytes,
                       substitutions[i].from_len) == 0) {
                /* Don't match if from == to (no-op substitution) */
                if (memcmp(substitutions[i].from_bytes,
                           substitutions[i].to_bytes,
                           substitutions[i].from_len) != 0) {
                    return i;
                }
            }
        }
    }
    return -1;
}
