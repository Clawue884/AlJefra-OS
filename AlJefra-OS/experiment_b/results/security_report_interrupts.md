# GPU Binary Evolution — Security & Vulnerability Report
## Component: interrupts
## Date: 2026-02-25
## GPU: NVIDIA RTX 5090 (170 SMs, 32GB VRAM) @ 100% utilization
## Runtime: ~4 hours | 64 functions | 38 analyzed | ~500 billion evaluations

---

## SECURITY SUMMARY

| Category | Count |
|----------|-------|
| Functions analyzed | 38 |
| FRAGILE (>5% fitness drop) | 5 |
| DEAD CODE (0 mutations) | 10 |
| Optimized | 13 |
| Skipped (too small) | 26 |

---

## FRAGILE FUNCTIONS (5-15% fitness drop from single mutation)

These functions are **most vulnerable** to binary-level tampering. A single register
bit flip or opcode change causes significant fitness degradation. These are the
functions that need the most hardening and should be integrity-checked at boot.

| Function | Fragility | Instrs | Bytes | Avg Mutations | Risk Description |
|----------|-----------|--------|-------|---------------|------------------|
| ps2_init | 6.3% | 36 | 150 | 15,308 | PS/2 keyboard init — hardware register writes, single flip breaks init |
| ps2_keyboard_interrupt | 5.8% | 29 | 56 | 4,102 | Keyboard IRQ handler — hot path, mutation-sensitive scancode processing |
| msix_init_enable | 5.8% | 23 | 72 | 24,282 | MSI-X interrupt enable — PCI config space writes are fragile |
| msix_init_create_entry | 5.5% | 23 | 51 | 12,223 | MSI-X table entry setup — wrong register = wrong interrupt vector |
| msi_init_enable | 5.1% | 21 | 84 | 16,536 | MSI interrupt enable — PCI config register sensitivity |

### Analysis
All 5 fragile functions are **interrupt setup code** that writes to hardware
configuration registers (MSI/MSI-X/IOAPIC/PS2). The hardware register addresses
and values are encoded directly in the instruction operands — a single bit flip
in a register field or immediate value can:
- Route interrupts to wrong vectors (privilege escalation risk)
- Disable interrupt masking (DoS risk)
- Corrupt PCI config space (device hijack risk)
- Break keyboard input (availability risk)

### Recommendations for Experiment A (AI-Directed)
1. Add runtime integrity checks (CRC32) on these function byte ranges
2. Consider read-only page mapping for interrupt setup code after boot
3. Validate MSI/MSI-X vector assignments against expected values
4. Add watchdog for interrupt delivery (detect silent failures)

---

## DEAD CODE (no mutations applied — data tables or unreachable)

These regions are labeled as functions but contain **no mutable code patterns**.
They are either data tables, pure I/O port sequences, or ultra-short stubs.

| Function | Instrs | Bytes | Likely Reason |
|----------|--------|-------|---------------|
| keylayoutlower | 23 | 59 | Keyboard scancode → ASCII lookup table (DATA, not code) |
| keylayoutupper | 23 | 59 | Keyboard scancode → ASCII lookup table (DATA, not code) |
| msix_init_error | 8 | 9 | Error handler — short jumps and returns only |
| msi_init_done | 6 | 6 | Cleanup stub — register restore + ret |
| msi_init_error | 6 | 6 | Error handler stub |
| ps2_flush | 5 | 12 | I/O port read loop (IN/OUT — not mutable) |
| serial_init | 5 | 25 | Serial port OUT instructions (I/O bound) |
| ps2_wait_read | 5 | 11 | I/O wait loop |
| keyboard_done | 3 | 3 | Return stub (pop + ret) |
| os_ioapic_redirection_error | 3 | 3 | Error return stub |

### Recommendations for Experiment A
1. Move keylayoutlower/upper to a .data section (saves decode cycles)
2. Error handlers are too small to optimize — consider inlining them
3. I/O-bound functions (ps2_flush, serial_init) won't benefit from CPU optimization

---

## FUNCTION-BY-FUNCTION SECURITY ANALYSIS

| # | Function | Fragility | Worst Fitness | Avg Mutations | Dead? | Improved? | Improvement |
|---|----------|-----------|---------------|---------------|-------|-----------|-------------|
| 1 | ps2_init | 6.3% | 64.60 | 15,308 | | YES | +0.35% |
| 2 | ps2_keyboard_interrupt | 5.8% | 64.84 | 4,102 | | | |
| 3 | msix_init_enable | 5.8% | 64.85 | 24,282 | | YES | +0.27% |
| 4 | msix_init_create_entry | 5.5% | 65.03 | 12,223 | | YES | +0.39% |
| 5 | keylayoutlower | 3.9% | 66.13 | 0 | YES | | |
| 6 | keylayoutupper | 3.9% | 66.13 | 0 | YES | | |
| 7 | msi_init_enable | 5.1% | 65.30 | 16,536 | | YES | +0.40% |
| 8 | os_ioapic_redirection | 4.8% | 65.51 | 16,540 | | YES | +0.50% |
| 9 | msix_init | 2.9% | 66.72 | 5,469 | | | |
| 10 | os_ioapic_mask_clear | 3.0% | 66.68 | 11,876 | | YES | +0.04% |
| 11 | serial_recv | 2.9% | 66.75 | 2,853 | | | |
| 12 | msix_init_error | 2.9% | 66.75 | 0 | YES | | |
| 13 | msi_init | 2.9% | 66.75 | 4,376 | | | |
| 14 | os_timer_init | 2.9% | 66.75 | 1,242 | | | |
| 15 | os_timer_init_phys | 3.3% | 66.48 | 1,066 | | YES | +0.04% |
| 16 | init_timer_hpet | 4.1% | 65.92 | 876 | | YES | +0.12% |
| 17 | msi_init_done | 2.8% | 66.86 | 0 | YES | | |
| 18 | serial_send_wait | 2.8% | 66.83 | 7,573 | | | |
| 19 | msi_init_error | 2.8% | 66.86 | 0 | YES | | |
| 20 | os_ioapic_write | 2.8% | 66.86 | 781 | | | |
| 21 | os_ioapic_init | 4.0% | 65.99 | 1,173 | | YES | +0.18% |
| 22 | os_ioapic_read | 2.6% | 66.98 | 781 | | | |
| 23 | serial_init | 2.6% | 66.98 | 0 | YES | | |
| 24 | ps2_wait_read | 2.6% | 66.98 | 0 | YES | | |
| 25 | serial_send_ready | 2.6% | 66.98 | 1,367 | | | |
| 26 | ps2_flush | 2.6% | 66.98 | 0 | YES | | |
| 27 | serial_interrupt | 2.6% | 66.98 | 1,459 | | | |
| 28 | ps2wait_loop | 3.8% | 66.13 | 2,187 | | YES | +0.11% |
| 29 | os_apic_write | 2.5% | 67.05 | 962 | | | |
| 30 | ps2_read_data | 3.8% | 66.18 | 911 | | | |
| 31 | os_timer_init_kvm | 2.9% | 66.80 | 706 | | YES | +0.03% |
| 32 | keyboard_processkey | 2.6% | 67.01 | 2,455 | | | |
| 33 | serial_recv_nochar | 2.5% | 67.07 | 2,734 | | | |
| 34 | keyboard_done | 2.5% | 67.07 | 0 | YES | | |
| 35 | serial_init_skip_arch | 2.5% | 67.06 | 3,971 | | YES | +0.05% |
| 36 | os_ioapic_redirection_error | 2.4% | 67.07 | 0 | YES | | |
| 37 | ps2_send_cmd | 2.4% | 67.03 | 1,215 | | | |
| 38 | os_apic_read | 3.6% | 66.24 | 1,953 | | YES | +0.15% |

---

## OPTIMIZATION SUMMARY

**Baseline fitness: 65.46 → Final fitness: 67.20 (+2.66%)**

13 functions improved through GPU-discovered binary mutations:

| Function | Improvement | Mutation Type |
|----------|------------|---------------|
| os_ioapic_redirection | +0.50% | Register field optimization in IOAPIC MMIO writes |
| msi_init_enable | +0.40% | MSI config register selection |
| msix_init_create_entry | +0.39% | MSI-X entry register optimization |
| ps2_init | +0.35% | PS/2 controller init register choice |
| msix_init_enable | +0.27% | MSI-X enable sequence register |
| os_ioapic_init | +0.18% | IOAPIC initialization register |
| os_apic_read | +0.15% | APIC register read optimization |
| init_timer_hpet | +0.12% | HPET timer init register |
| ps2wait_loop | +0.11% | PS/2 wait loop register |
| serial_init_skip_arch | +0.05% | Serial port setup |
| os_timer_init_phys | +0.04% | Physical timer init |
| os_ioapic_mask_clear | +0.04% | IOAPIC mask clear |
| os_timer_init_kvm | +0.03% | KVM timer init |

---

## METHOD

- **Engine**: AlJefra OS AI Binary Evolution Engine (Experiment B)
- **GPU**: NVIDIA RTX 5090, 65,536 parallel threads per function
- **Mutation strategies**: Pattern substitution, ModRM register field mutation, NOP swap, REX prefix toggle
- **Ratio allocation**: 750K iters (huge) → 500K (large) → 350K (medium) → 150K (small) → 50K (tiny)
- **Security tracking**: Per-thread worst-fitness tracking across all mutations, dead code detection via mutation count
- **Total evaluations**: ~500 billion across all functions
