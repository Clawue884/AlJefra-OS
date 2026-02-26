# GPU Binary Evolution — Security & Vulnerability Report
## Component: storage
## Date: 2026-02-26
## GPU: NVIDIA RTX 5090 (170 SMs, 32GB VRAM) @ 100% utilization
## Runtime: ~5 hours | 64 functions | 50 analyzed | ~800 billion evaluations

---

## SECURITY SUMMARY

| Category | Count |
|----------|-------|
| Functions analyzed | 50 |
| CRITICAL (>15% fitness drop) | 13 |
| FRAGILE (5-15% drop) | 37 |
| ROBUST (<1% drop) | 0 |
| DEAD CODE | 8 |
| Optimized | 31 |

### Safety System Validated
b_net_tx found a +0.29% improvement but was **REJECTED** because the mutation
removed a LOCK prefix at 0x100a5b — the safety checker correctly blocked an
atomicity-breaking mutation. The system works.

---

## 🔴 CRITICAL VULNERABILITIES (>15% fitness drop)

| Function | Fragility | Instrs | Bytes | Avg Mutations | Risk |
|----------|-----------|--------|-------|---------------|------|
| os_bus_read_bar | 16.6% | 51 | 142 | 54,363 | PCI BAR read — device MMIO mapping |
| os_debug_block | 16.2% | 33 | 107 | 20,555 | Debug block output — memory dump path |
| os_debug_dump_al | 16.1% | 26 | 80 | 2,871 | Debug register dump |
| b_net_tx | 16.0% | 23 | 84 | 18,218 | Network transmit — LOCK prefix critical |
| nextchar | 16.0% | 19 | 53 | 22,692 | Character parsing — buffer traversal |
| os_debug_dump_mem | 15.8% | 15 | 41 | 6,632 | Memory dump — info leak vector |
| os_debug_block_nextline | 15.4% | 12 | 24 | 7,632 | Debug line advancement |
| os_debug_dump_rax | 15.3% | 9 | 40 | 7,383 | 64-bit register dump |
| os_bus_cap_check | 15.2% | 8 | 23 | 8,485 | PCI capability check |
| os_debug_string | 15.1% | 16 | 35 | 13,438 | String output — no improvement found |
| os_debug_space | 15.1% | 8 | 20 | 1,640 | Space output |
| init_net | 15.0% | 7 | 37 | 905 | Network init dispatcher |
| b_output_serial_send | 15.0% | 7 | 13 | 876 | Serial byte output |

### Storage-Specific Risks
- **b_nvs_read_sector** (15.0% fragility): Wrong sector number → data corruption
- **b_nvs_write_sector** (14.8% fragility): Wrong sector → persistent disk corruption
- **b_nvs_write** (14.3% fragility): Write loop — wrong count → partial writes
- **b_nvs_read** (14.2% fragility): Read loop — wrong count → incomplete reads

---

## 🟡 FRAGILE — Storage-Specific Attack Surface

| Function | Fragility | Instrs | Risk Description |
|----------|-----------|--------|------------------|
| b_nvs_read_sector | 15.0% | 13 | Sector read — wrong LBA = data corruption |
| b_net_config | 14.9% | 13 | NIC configuration — MAC/MTU hijack |
| b_nvs_write_sector | 14.8% | 13 | Sector write — wrong LBA = persistent corruption |
| b_net_status | 14.8% | 14 | NIC status — state spoofing |
| b_nvs_write | 14.3% | 10 | Write dispatch — loop integrity |
| b_nvs_read | 14.2% | 9 | Read dispatch — loop integrity |
| b_input | 14.0% | 4 | User input — buffer pointer validation |
| init_nvs | 14.1% | 5 | Storage init — driver binding |
| init_nvs_virtio_blk | 14.1% | 4 | VirtIO block init |
| init_nvs_check_bus | 14.2% | 8 | NVS bus enumeration |
| init_nvs_done | 14.3% | 5 | Storage init completion |

### Recommendations for Experiment A
1. **b_nvs_read/write_sector**: Add LBA bounds checking against partition size
2. **b_nvs_write**: Verify write count doesn't exceed remaining sectors
3. **b_net_tx**: LOCK prefix must be preserved — consider marking as immutable
4. **os_debug_dump_mem**: Validate memory range before dumping (info leak risk)
5. **os_bus_read_bar**: Validate BAR addresses against known PCIe ranges

---

## ⚪ DEAD CODE

| Function | Instrs | Bytes | Reason |
|----------|--------|-------|--------|
| b_output_serial_next | 8 | 18 | Serial output helper — possibly inlined |
| os_debug_dump_mem_done | 7 | 9 | Debug completion stub |
| b_net_config_end | 5 | 5 | Config return stub |
| os_bus_cap_check_done | 3 | 3 | Return-only stub |
| b_net_status_end | 3 | 3 | Return-only stub |
| os_bus_cap_check_error | 3 | 3 | Error return stub |
| b_output_serial | 3 | 3 | Serial output entry (possibly forwarding) |
| b_net_tx_fail | 3 | 3 | TX failure return stub |

---

## OPTIMIZATION RESULTS

**Baseline: 59.29 → Final: 61.22 (+3.25%)**
**31 of 50 functions improved (62% hit rate)**

Top optimizations:
| Function | Improvement |
|----------|------------|
| os_bus_read_bar | +0.52% |
| nextchar | +0.28% |
| b_output_serial_send | +0.18% |
| os_debug_block_nextline | +0.15% |
| os_bus_cap_check_next | +0.14% |
| init_hid_done | +0.13% |
| init_sys_done | +0.13% |
| os_debug_dump_mem | +0.12% |
| os_debug_dump_rax | +0.12% |
| b_net_config | +0.12% |
| init_nvs_done | +0.11% |
| init_net_probe_not_found | +0.10% |
| os_debug_dump_ax | +0.10% |
| b_nvs_read_sector | +0.08% |
| b_nvs_write_sector | +0.08% |
| os_bus_cap_check | +0.08% |
| os_debug_newline | +0.08% |
| init_net_probe_find_next_driver | +0.08% |
| b_input | +0.08% |
| os_debug_dump_al | +0.07% |
| os_debug_space | +0.07% |
| os_debug_block | +0.06% |
| init_net_probe_find_next_device | +0.06% |
| init_nvs_virtio_blk | +0.06% |
| init_nvs | +0.05% |
| init_net | +0.04% |
| os_bus_read_bar_32bit | +0.02% |
| init_net_probe_find_driver | +0.02% |
| os_debug_dump_eax | +0.02% |
| b_net_status | +0.02% |
| init_net_probe_found | +0.02% |

---

## CROSS-COMPONENT COMPARISON

| Metric | kernel | network | interrupts | storage |
|--------|--------|---------|------------|---------|
| Functions analyzed | 50 | 48 | 38 | 50 |
| CRITICAL | **35 (70%)** | 2 (4%) | 0 (0%) | 13 (26%) |
| FRAGILE | 15 (30%) | **46 (96%)** | 5 (13%) | 37 (74%) |
| ROBUST | 0 | 0 | 0 | 0 |
| Dead code | 3 | 2 | 10 | 8 |
| Improved | 34 (68%) | 28 (58%) | 13 (34%) | **31 (62%)** |
| Max fragility | **17.8%** | 15.8% | 6.3% | 16.6% |
| Fitness gain | +3.24% | **+4.54%** | +2.66% | +3.25% |

### Overall Findings
- **kernel** remains most dangerous — 70% critical, highest fragility (17.8%)
- **storage** is #2 — 26% critical, persistent corruption risk via NVS functions
- **network** has the most remote attack surface (b_net_tx/rx)
- **interrupts** is the most resilient (0 critical, max 6.3%)
- **No component has ANY robust function** — entire OS is mutation-sensitive
- **Safety system validated**: LOCK prefix removal correctly blocked on b_net_tx

---

## METHOD

- **Engine**: AlJefra OS AI Binary Evolution Engine (Experiment B)
- **GPU**: NVIDIA RTX 5090, 65,536 parallel threads per function
- **Mutation strategies**: Pattern substitution, ModRM register field, NOP swap, REX prefix toggle
- **Ratio allocation**: 750K (huge 51+) → 500K (large 31-50) → 350K (medium 16-30) → 150K (small 8-15) → 50K (tiny <8)
