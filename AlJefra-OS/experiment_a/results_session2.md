# Experiment A: AI-Directed Source-Level Optimization — Session 2

**Date:** 2026-02-26
**Analyst:** Claude Opus 4.6

## Critical Bug Fixes Applied

### 1. system.asm: b_system dispatch — CRITICAL
- **Line 30:** `movzx ecx, word [ecx]` → `mov cx, [ecx]`
- **Impact:** ALL b_system calls jumped to wrong address (0x0000xxxx instead of 0x0010xxxx)
- **Root cause:** `movzx` zeros upper ECX bits containing kernel base address
- **Status:** FIXED

### 2. smp.asm: b_smp_set — CRITICAL
- **Line 115:** `jnc b_smp_set_error` → `jz b_smp_set_error`
- **Impact:** b_smp_set always returned error (test always clears CF, jnc always taken)
- **Root cause:** Evolution changed `bt cl, 0; jnc` to `test cl, 1` but kept `jnc`
- **Status:** FIXED

### 3. timer.asm: HPET init — HIGH
- **Line 64-65:** Added `test rax, rax` between `mov rax, [os_HPET_Address]` and `jz`
- **Impact:** `jz` tested stale flags (mov doesn't set flags)
- **Status:** FIXED

### 4. io.asm: b_output_serial RCX=0 guard — MEDIUM
- **Lines 39-41:** Added `test ecx, ecx` / `jz b_output_serial_done` guard
- **Impact:** RCX=0 caused dec to wrap → near-infinite loop
- **Status:** FIXED

## Peephole Optimizations Applied

| File | Optimization | Savings |
|------|-------------|---------|
| bus.asm | Removed dead `xor eax, eax` + `xor ebx, ebx` | 4 bytes, 2 instrs |
| bus.asm | `mov rax, rbx` → `mov eax, ebx` (×2) | 2 bytes (REX prefix) |
| kernel.asm | `and al, 0xF0` + `test rax, rax` → `and rax, -16` | 1 byte, 1 instr |
| timer.asm | `xor ecx, ecx` + `mov cl, [...]` → `movzx ecx, byte [...]` | 2 bytes, 1 instr |
| timer.asm | `mov ecx, 1000` + `mul rcx` → `imul rax, rax, 1000` | 1 instr, no clobber |
| debug.asm | `shr bl, 4` → `shr ebx, 4` | 0 bytes, avoids stall |
| system.asm | Removed dead `xor eax, eax` before b_smp_get_id | 2 bytes, 1 instr |

## Build Verification
- x86-64 ASM kernel: PASS (bash aljefra.sh build)
- x86-64 HAL kernel: 145 KB ✓
- ARM64 HAL kernel: 153 KB ✓
- RISC-V HAL kernel: 129 KB ✓

## Cumulative Savings
- **Bug fixes:** 4 critical/high/medium correctness issues resolved
- **Code size:** ~11 bytes reduced
- **Instructions:** ~7 fewer instructions across hot paths
- **uops:** ~6-8 fewer micro-ops in critical paths
