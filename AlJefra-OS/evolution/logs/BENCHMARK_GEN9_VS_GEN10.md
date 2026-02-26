# Benchmark: GEN9 vs GEN10

**Generated:** 2026-02-25 23:22 UTC
**Tool:** benchmark_compare.sh | NASM version 2.16.03
**Base:** commit `0269721` (GEN9)
**Target:** commit `aa7fe4d` (GEN10)

> Both kernels built with `-dNO_VGA`. Target additionally uses `-dNO_GPU` to exclude
> GPU/evolution/AI code added post-fork. Comparison covers **shared original components only**.

---

## 1. Summary

| Metric | GEN9 | GEN10 | Delta | Change |
|--------|-------------|---------------|-------|--------|
| **Binary size** (bytes) | 20480 | 20480 | 0 | 0.0% |
| **Total instructions** | 4804 | 4774 | -30 | -0.6% |
| **Code bytes** | 12803 | 12766 | -37 | -0.3% |
| **Code density** (B/instr) | 2.665 | 2.674 | — | — |
| **Boot time (QEMU)** | N/Ams | N/Ams | N/Ams | — |

---

## 2. Optimization Pattern Counts

| Pattern | Description | GEN9 | GEN10 | Delta |
|---------|-------------|-------------|---------------|-------|
| `test` | Zero-comparison (efficient) | 70 | 71 | +1 |
| `cmp *, 0` | Zero-comparison (replaced by test) | 7 | 7 | 0 |
| `lea` | Address calc (replaces shl+add) | 13 | 13 | 0 |
| `pause` | Spin-wait hint | 7 | 10 | +3 |
| `bt/bts/btr` | Bit-test operations | 45 | 45 | 0 |
| `imul` | Signed multiply (no EDX clobber) | 2 | 19 | +17 |
| `mul` | Unsigned multiply (EDX clobbered) | 21 | 8 | -13 |
| `movzx` | Zero-extending loads | 14 | 21 | +7 |
| `call os_apic_*` | APIC calls (inlined in Gen-10) | 12 | 2 | -10 |
| `prefetchnta` | Cache prefetch hints | 13 | 13 | 0 |
| `rep stosq` | 64-bit memory fill | 7 | 7 | 0 |
| `xchg` | Implicit LOCK exchange | 12 | 12 | 0 |

---

## 3. Per-Component Comparison

| Component | IC (base) | IC (target) | ΔIC | IC% | CB (base) | CB (target) | ΔCB | CB% |
|-----------|-----------|-------------|-----|-----|-----------|-------------|-----|-----|
| `kernel.asm` | 75 | 75 | 0 | 0.0% | 281 | 281 | 0 | 0.0% |
| `init.asm` | 0 | 0 | 0 | N/A | 0 | 0 | 0 | N/A |
| `init/64.asm` | 138 | 138 | 0 | 0.0% | 542 | 542 | 0 | 0.0% |
| `init/bus.asm` | 70 | 70 | 0 | 0.0% | 217 | 217 | 0 | 0.0% |
| `init/hid.asm` | 4 | 4 | 0 | 0.0% | 16 | 16 | 0 | 0.0% |
| `init/net.asm` | 57 | 57 | 0 | 0.0% | 217 | 217 | 0 | 0.0% |
| `init/nvs.asm` | 22 | 22 | 0 | 0.0% | 82 | 82 | 0 | 0.0% |
| `init/sys.asm` | 12 | 12 | 0 | 0.0% | 58 | 58 | 0 | 0.0% |
| `syscalls.asm` | 0 | 0 | 0 | N/A | 0 | 0 | 0 | N/A |
| `syscalls/bus.asm` | 91 | 91 | 0 | 0.0% | 252 | 252 | 0 | 0.0% |
| `syscalls/debug.asm` | 159 | 159 | 0 | 0.0% | 469 | 469 | 0 | 0.0% |
| `syscalls/io.asm` | 26 | 26 | 0 | 0.0% | 65 | 65 | 0 | 0.0% |
| `syscalls/net.asm` | 80 | 80 | 0 | 0.0% | 230 | 230 | 0 | 0.0% |
| `syscalls/nvs.asm` | 45 | 45 | 0 | 0.0% | 117 | 117 | 0 | 0.0% |
| `syscalls/smp.asm` | 178 | 168 | -10 | -5.6% | 464 | 453 | -11 | -2.4% |
| `syscalls/system.asm` | 107 | 107 | 0 | 0.0% | 371 | 371 | 0 | 0.0% |
| `drivers.asm` | 0 | 0 | 0 | N/A | 0 | 0 | 0 | N/A |
| `drivers/apic.asm` | 22 | 22 | 0 | 0.0% | 67 | 67 | 0 | 0.0% |
| `drivers/ioapic.asm` | 54 | 54 | 0 | 0.0% | 147 | 147 | 0 | 0.0% |
| `drivers/msi.asm` | 107 | 107 | 0 | 0.0% | 265 | 265 | 0 | 0.0% |
| `drivers/ps2.asm` | 140 | 140 | 0 | 0.0% | 519 | 519 | 0 | 0.0% |
| `drivers/serial.asm` | 46 | 46 | 0 | 0.0% | 121 | 121 | 0 | 0.0% |
| `drivers/timer.asm` | 160 | 159 | -1 | -0.6% | 480 | 479 | -1 | -0.2% |
| `drivers/virtio.asm` | 0 | 0 | 0 | N/A | 0 | 0 | 0 | N/A |
| `drivers/bus/pci.asm` | 26 | 26 | 0 | 0.0% | 63 | 63 | 0 | 0.0% |
| `drivers/bus/pcie.asm` | 40 | 40 | 0 | 0.0% | 101 | 101 | 0 | 0.0% |
| `drivers/nvs/virtio-blk.asm` | 230 | 230 | 0 | 0.0% | 702 | 702 | 0 | 0.0% |
| `drivers/net/virtio-net.asm` | 378 | 378 | 0 | 0.0% | 1165 | 1165 | 0 | 0.0% |
| `drivers/lfb/lfb.asm` | 18 | 18 | 0 | 0.0% | 72 | 72 | 0 | 0.0% |
| `interrupt.asm` | 349 | 344 | -5 | -1.4% | 1232 | 1226 | -6 | -0.5% |
| `sysvar.asm` | 17 | 17 | 0 | 0.0% | 105 | 105 | 0 | 0.0% |
| **TOTAL** | **2651** | **2635** | **-16** | **-0.6%** | **8420** | **8402** | **-18** | **-0.2%** |

---

## 4. Hot-Path Function Analysis

| Function | IC (base) | IC (target) | ΔIC | IC% | Bytes (base) | Bytes (target) | ΔBytes | B% |
|----------|-----------|-------------|-----|-----|-------------|----------------|--------|-----|
| `ap_clear` | 10 | 10 | 0 | 0.0% | 40 | 40 | 0 | 0.0% |
| `b_smp_lock` | 7 | 7 | 0 | 0.0% | 18 | 18 | 0 | 0.0% |
| `b_smp_unlock` | 2 | 2 | 0 | 0.0% | 4 | 4 | 0 | 0.0% |
| `ap_wakeup` | 7 | 7 | 0 | 0.0% | 25 | 25 | 0 | 0.0% |
| `b_smp_wakeup` | 38 | 33 | -5 | -13.2% | 108 | 102 | -6 | -5.6% |
| `b_smp_get_id` | 6 | 4 | -2 | -33.3% | 17 | 15 | -2 | -11.8% |
| `b_smp_wakeup_all` | 18 | 16 | -2 | -11.1% | 52 | 50 | -2 | -3.8% |
| `b_smp_reset` | 20 | 17 | -3 | -15.0% | 56 | 52 | -4 | -7.1% |
| `lfb_draw_line` | 41 | 32 | -9 | -22.0% | 164 | 144 | -20 | -12.2% |
| `hpet_ns` | 12 | 11 | -1 | -8.3% | 35 | 33 | -2 | -5.7% |

---

## 5. Optimization Categories

| Category | Description | Key Changes |
|----------|-------------|-------------|
| **Peephole** | Instruction substitution (test/cmp, inc/add, imul/mul) | `mul ecx` → `imul eax, ecx`, `xor+mov` → `movzx` |
| **Dead code** | Removing unused instructions | Dead `push/pop rcx` in interrupt handlers, dead `xor edx` |
| **Inlining** | Eliminating call/ret overhead | APIC read/write inlined in `b_smp_*` functions |
| **Loop restructure** | Top-test → bottom-test | `hpet_wake_idle_core` scan loop |
| **Strength reduction** | Replacing expensive ops with cheaper ones | `mul ecx, 4` → `shl eax, 2`, indexed addressing in `hpet_write` |
| **Register save elim** | Removing unnecessary push/pop pairs | `push/pop rdx` removed from `lfb_pixel`, `lfb_draw_line`, `lfb_update_cursor` |

---

## 6. Evolution History

| Gen | Component | Description |
|-----|-----------|-------------|
| 1 | kernel | Gen-1: Interrupt storm fix, spinlock evolution, non-temporal init, preemptive scheduling, work stealing, APIC optimization, branchless hex dump |
| 1 | drivers | Gen-1: Spin-wait pause in virtio-net/blk/timer/kvm, 64-bit MMIO stores, branchless LFB font rendering, rep stosq/movsq screen clear and glyph copy, MSI-X loop hoist, IOAPIC mfence, bus table prefetch |
| 1 | smp | Gen-1: PAUSE in all spinlock/IPI spin-wait loops (b_smp_lock TTAS, reset_wait, wakeup_wait, wakeup_all_wait), partial register fixes (inc cx->ecx), test vs cmp optimization, xchg->mov in b_smp_set, 32-bit shr in get_id |
| 1 | interrupts | Gen-1: movzx eax,al replacing and eax,0xFF (shorter encoding), inc ebx replacing add ebx,1 |
| 1 | io | Gen-1: b_output tail-call jmp (eliminates call+ret overhead), dec ecx replacing dec cx (partial register stall fix) |
| 1 | syscalls | Gen-1: 14 tail-call optimizations (call+ret->jmp) across all system dispatch wrappers, cmp ecx replacing cmp rcx for bounds check, and rax,~0xFF replacing shr/shl pair in virt_to_phys |
| 1 | network | Gen-1: test al,al replacing cmp al,0 in all interface checks, test ecx,ecx in rx data check, cmp ecx,1522 replacing cmp cx,1522, xchg->mov in virt_to_phys call |
| 1 | storage | Gen-1: dec r8 replacing sub r8,1 in read/write loops, xchg->mov+push/pop in virt_to_phys calls |
| 1 | memory | Gen-1: 32-bit dec ecx replacing dec rcx/dec cx in gate setup loops, test rax,rax replacing cmp rax,0, 32-bit test ecx replacing test cx in AP init |
| 1 | bus | Gen-1: 4 tail-call optimizations (call+ret->jmp) in bus read/write for PCI and PCIe paths, test al,al replacing cmp al,0x00 in capability linked list walk |
| 1 | timer | Gen-1: PAUSE in HPET delay spin loop, PAUSE in KVM ns version wait and delay wait, test rax,rax and test cl,cl replacing cmp with 0, branch restructure eliminating extra jmp |
| 1 | serial | Gen-1: PAUSE in serial_send_wait spin loop, test al,0x20/0x01 replacing and+cmp pattern in send_wait and recv |
| 1 | debug | Gen-1: xor edx,edx replacing mov dx,0, test rcx,rcx replacing cmp rcx,0, branch restructure eliminating extra jmp |
| 2 | smp | Gen-2 DEEP: b_smp_busy bounded by os_NumCores (not 256), LOCK BTS atomic setflag, LEA-based SMP table indexing, b_smp_get direct index |
| 2 | kernel | Gen-2 DEEP: MONITOR/MWAIT replacing HLT (sub-microsecond wakeup on SMP table write), xor r8d-r15d for register clearing (saves REX.W bytes) |
| 2 | syscalls | Gen-2 DEEP: movzx ecx in dispatch (fixes partial register stall), 5 movzx getters replacing xor+mov pairs, 32-bit ops in reset loop |
| 2 | debug | Gen-2 DEEP: Branchless hex dump via 16-byte lookup table (hex_lut), eliminates 4 conditional branches per byte, 32-bit inc edx in mem dump |
| 2 | virtio-net | Gen-2 DEEP: PAUSE in device reset + transmit wait spin loops, dec ecx sets ZF (eliminates cmp), xor eax replaces mov ax 0, inc al replaces add al 1, test al replaces cmp al 0 throughout |
| 2 | virtio-blk | Gen-2 DEEP: PAUSE in device reset + I/O wait spin loops, test bx/al replacing cmp with 0, inc al replacing add al 1, xor eax replacing mov ax 0 in avail ring |
| 2 | timer | Gen-2 DEEP: SHRD instruction replacing 3-instruction shl+shr+or sequence in kvm_ns nanosecond calculation |
| 2 | lfb | Gen-2 DEEP: Branchless CMOV font rendering, rep stosq 2x screen clear throughput (64-bit packed pixels), tail-call lfb_output_char, test rax replacing cmp rax 0, inc ax replacing add ax 1 |
| 2 | init | Gen-2 DEEP: create_gate uses direct mov writes instead of stosw+add rdi (eliminates auto-increment overhead) |
| 3 | smp | Gen-3: LEA-based b_smp_set indexing, store-release unlock (mov replacing btr RMW), prefetchnta in busy scan |
| 3 | kernel | Gen-3: LEA-based SMP table indexing in start_payload and ap_clear, align 16 on hot ap_check scheduling loop |
| 3 | syscalls | Gen-3: LEA-based PD table indexing in os_virt_to_phys (eliminates shl+add), saves 1 instruction per virt-to-phys translation |
| 3 | timer | Gen-3: Removed dead xor edx in hpet_ns, cached HPET base+counter in register for direct MMIO polling in delay loop (eliminates call overhead per iteration) |
| 3 | lfb | Gen-3: rep stosq 2x throughput in all lfb_draw_line fills, LEA-based pixel addressing in lfb_pixel, imul replacing mul (removed push/pop rdx), partial register fix dec cl->ecx in cursor |
| 3 | virtio-net | Gen-3: Direct memory compare in transmit wait (eliminates mov ax,[rdi]), prefetchnta in config descriptor loop |
| 3 | virtio-blk | Gen-3: Direct memory compare in I/O wait spin loop (cmp [rdi],bx replacing mov+cmp) |
| 3 | interrupts | Gen-3: Inline APIC EOI in ap_wakeup and hpet handlers (eliminates call overhead, saves 2 push/pop pairs each), imul replacing mul*bl in exception handler |
| 3 | debug | Gen-3: Reduced push/pop overhead in os_debug_dump_al (merged register saves), rep stosq 2x throughput in os_debug_block pixel fill |
| 4 | smp | Gen-4 INLINE: b_smp_get inlined APIC ID read (eliminates 2 nested calls), b_smp_setflag inlined APIC read, b_smp_busy inlined APIC read, test byte replacing bt word in lock, store-based unlock |
| 4 | kernel | Gen-4 INLINE: ap_halt inlined APIC read for MONITOR, ap_process inlined setflag+APIC read (3 calls eliminated), mov eax,1 replacing xor+or in SMP init |
| 4 | network | Gen-4: LEA-based lock address calculation (lea rax,[rdx+nt_lock] replacing mov+add) |
| 4 | debug | Gen-4: or rcx,-1 replacing xor+not in strlen (1 instruction vs 2) |
| 4 | init | Gen-4: xor ebx,ebx replacing mov ebx,0 (smaller encoding) |
| 9 | init/bus | Gen-9: mov al,N replacing bts ax,N (avoids partial register stall on 16-bit bts after 32-bit xor) |
| 9 | pcie | Gen-9: or rdx,-1 replacing xor edx+not rdx (1 instruction vs 2, 3 bytes vs 5) |
| 9 | ioapic | Gen-9: inc ecx replacing add ecx,1 (shorter encoding, 2 instances) |
| 9 | system | Gen-9: and rax,-256 replacing shr+shl pair in os_virt_to_phys (1 instruction vs 2) |
| 9 | init/64 | Gen-9: 32-bit dec/test replacing 64/16-bit (avoids REX prefix and partial register stall, 3 instances) |
| 9 | lfb | Gen-9: and ax,0xFFF8 replacing shr+shl pair, dec ecx replacing dec cx, inc ax replacing add ax,1 (5 optimizations) |
| 9 | bus | Gen-9: or eax,-1 replacing xor+not in BAR sizing (1 instruction vs 2) |
| 9 | smp | Gen-9: test al,al replacing cmp al,0 in b_smp_set alignment check |
| 9 | virtio-blk | Gen-9: xor eax,eax replacing mov ax,0, inc replacing add-1 (2 optimizations) |
| 9 | virtio-net | Gen-9: xor eax,eax replacing mov ax,0, inc replacing add-1 (3 optimizations) |
| 9 | debug | Gen-9: imul replacing mul (avoids EDX clobber), 32-bit ops replacing 16/64-bit (avoids partial reg stall, 4 optimizations) |
| 10 | interrupts | Gen-10: Dead register save elimination (push/pop rcx in int_serial, hpet_wake_idle_core), bottom-test loop restructure in hpet_wake_idle_core .scan (saves 1 jmp) |
| 10 | timer | Gen-10: Dead xor edx,edx removal in hpet_ns (overwritten by mul), indexed addressing in hpet_write [rsi+rcx] (matches hpet_read pattern) |
| 10 | lfb | Gen-10: 13x mul->imul replacement (avoids EDX clobber), shl replacing mul-by-4 (strength reduction), push/pop rdx elimination in 3 functions (lfb_update_cursor, lfb_pixel, lfb_draw_line), movzx+imul chain in lfb_draw_line row/clear calc (~9 instructions saved) |
| 10 | smp | Gen-10: Full APIC inlining in b_smp_get_id, b_smp_wakeup, b_smp_wakeup_all, b_smp_reset (eliminates 12 call/ret pairs, ~36 instructions saved via direct MMIO access) |

**Cumulative from baseline (0269721):** 4804 instructions, 12803 code bytes
**Current (aa7fe4d):** 4774 instructions, 12766 code bytes
**Net reduction:** -30 instructions (-0.6%), -37 code bytes (-0.3%)

---

## 7. Methodology

- **Static analysis** via NASM listing files (`-l` flag) — maps every source line to its binary encoding
- **Pattern counting** via grep across shared `.asm` source files
- **Per-component tracking** by parsing `%include` boundaries in NASM listings
- **Code density** = total code bytes / total instructions (lower = more efficient encoding)
- **Fair comparison**: Both kernels built with `-dNO_VGA`; target with `-dNO_GPU` to exclude post-fork additions
- **Shared files**: 31 components present in both base and target
- **QEMU timing**: Wall-clock boot to serial output (if bootloader available)

---

*Report generated by `evolution/benchmark_compare.sh`*
