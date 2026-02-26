# Benchmark Comparison: Baseline vs Gen-8

**Generated:** 2026-02-25 22:40 UTC
**Tool:** benchmark_compare.sh | NASM version 2.16.03
**Baseline:** commit `fd5a707` — Original AlJefra OS fork
**Gen-8:** commit `0269721` — 8 generations of AI-directed evolution (~200+ optimizations)

> Both kernels built with `-dNO_VGA`. Gen-8 additionally uses `-dNO_GPU` to exclude
> GPU/evolution/AI code added post-fork. Comparison covers **shared original components only**.

---

## 1. Summary

| Metric | Baseline | Gen-8 | Delta | Change |
|--------|----------|-------|-------|--------|
| **Binary size** (bytes) | 20480 | 20480 | 0 | 0.0% |
| **Total instructions** | 4155 | 4804 | 649 | 15.6% |
| **Code bytes** | 10770 | 12803 | 2033 | 18.9% |
| **Avg instruction length** | 2.59B | 2.67B | — | — |
| **Boot time (QEMU)** | N/Ams | N/Ams | N/Ams | — |

---

## 2. Optimization Pattern Counts

Instruction-level patterns compared across all shared `.asm` source files.

| Pattern | Description | Baseline | Gen-8 | Delta | Direction |
|---------|-------------|----------|-------|-------|-----------|
| `test` | Zero-comparison (efficient) | 15 | 70 | +55 ↑ |
| `cmp *, 0` | Zero-comparison (replaced by test) | 18 | 7 | -11 ↓ |
| `lea` | Address calc (replaces shl+add) | 1 | 13 | +12 ↑ |
| `pause` | Spin-wait hint (new in Gen-8) | 0 | 7 | +7 ↑ |
| `bt/bts/btr` | Bit-test operations | 43 | 45 | +2 ↑ |
| `jmp` | Jumps (incl. tail-call conversions) | 101 | 141 | +40 ↑ |
| `prefetchnta` | Cache prefetch hints | 0 | 13 | +13 ↑ |
| `xchg` | Implicit LOCK exchange (eliminated) | 13 | 12 | -1 ↓ |
| `rep stosq` | 64-bit memory fill | 1 | 7 | +6 ↑ |
| `rep stosd` | 32-bit memory fill (replaced) | 6 | 7 | +1 ↑ |
| `call os_apic_write` | APIC write calls (inlined in Gen-8) | 11 | 6 | -5 ↓ |
| Inline EOI | Direct APIC EOI writes | 0 | 0 | 0 — |
| `movzx` | Zero-extending loads | 0 | 14 | +14 ↑ |
| `xor eax, eax` | 32-bit zero idiom | 84 | 106 | +22 ↑ |

---

## 3. Per-Component Comparison

Source lines (SL), instruction count (IC), and code bytes (CB) for each shared component.

| Component | SL (base) | SL (gen8) | ΔSL | IC (base) | IC (gen8) | ΔIC | CB (base) | CB (gen8) | ΔCB |
|-----------|-----------|-----------|-----|-----------|-----------|-----|-----------|-----------|-----|
| `kernel.asm` | 144 | 168 | +24 | 76 | 75 | -1 | 267 | 281 | +14 |
| `init.asm` | 18 | 19 | +1 | 0 | 0 | 0 | 0 | 0 | 0 |
| `init/64.asm` | 210 | 223 | +13 | 129 | 138 | +9 | 519 | 542 | +23 |
| `init/bus.asm` | 143 | 143 | 0 | 70 | 70 | 0 | 226 | 217 | -9 |
| `init/hid.asm` | 27 | 27 | 0 | 4 | 4 | 0 | 16 | 16 | 0 |
| `init/net.asm` | 95 | 103 | +8 | 52 | 57 | +5 | 196 | 217 | +21 |
| `init/nvs.asm` | 53 | 55 | +2 | 21 | 22 | +1 | 78 | 82 | +4 |
| `init/sys.asm` | 39 | 39 | 0 | 12 | 12 | 0 | 58 | 58 | 0 |
| `syscalls.asm` | 19 | 22 | +3 | 0 | 0 | 0 | 0 | 0 | 0 |
| `syscalls/bus.asm` | 183 | 178 | -5 | 96 | 91 | -5 | 253 | 252 | -1 |
| `syscalls/debug.asm` | 244 | 250 | +6 | 160 | 159 | -1 | 452 | 469 | +17 |
| `syscalls/io.asm` | 72 | 71 | -1 | 27 | 26 | -1 | 67 | 65 | -2 |
| `syscalls/net.asm` | 174 | 174 | 0 | 81 | 80 | -1 | 231 | 230 | -1 |
| `syscalls/nvs.asm` | 89 | 91 | +2 | 43 | 45 | +2 | 113 | 117 | +4 |
| `syscalls/smp.asm` | 288 | 370 | +82 | 147 | 178 | +31 | 383 | 464 | +81 |
| `syscalls/system.asm` | 421 | 428 | +7 | 126 | 107 | -19 | 399 | 371 | -28 |
| `drivers.asm` | 53 | 71 | +18 | 0 | 0 | 0 | 0 | 0 | 0 |
| `drivers/apic.asm` | 82 | 103 | +21 | 12 | 22 | +10 | 43 | 67 | +24 |
| `drivers/ioapic.asm` | 127 | 133 | +6 | 52 | 54 | +2 | 143 | 147 | +4 |
| `drivers/msi.asm` | 195 | 196 | +1 | 107 | 107 | 0 | 272 | 265 | -7 |
| `drivers/ps2.asm` | 305 | 309 | +4 | 137 | 140 | +3 | 522 | 519 | -3 |
| `drivers/serial.asm` | 148 | 147 | -1 | 47 | 46 | -1 | 123 | 121 | -2 |
| `drivers/timer.asm` | 364 | 366 | +2 | 158 | 160 | +2 | 478 | 480 | +2 |
| `drivers/virtio.asm` | 94 | 94 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| `drivers/bus/pci.asm` | 92 | 92 | 0 | 26 | 26 | 0 | 63 | 63 | 0 |
| `drivers/bus/pcie.asm` | 112 | 111 | -1 | 41 | 40 | -1 | 102 | 101 | -1 |
| `drivers/nvs/virtio-blk.asm` | 421 | 416 | -5 | 237 | 230 | -7 | 729 | 702 | -27 |
| `drivers/net/virtio-net.asm` | 634 | 623 | -11 | 393 | 378 | -15 | 1207 | 1165 | -42 |
| `drivers/lfb/lfb.asm` | 635 | 650 | +15 | 18 | 18 | 0 | 72 | 72 | 0 |
| `interrupt.asm` | 447 | 513 | +66 | 309 | 349 | +40 | 1078 | 1232 | +154 |
| `sysvar.asm` | 181 | 181 | 0 | 17 | 17 | 0 | 95 | 105 | +10 |
| **TOTAL (shared)** | **6109** | **6366** | **+257** | **2598** | **2651** | **+53** | **8185** | **8420** | **+235** |

### Gen-8 Only Components (not in baseline)

| Component | Source Lines | Notes |
|-----------|-------------|-------|
| `drivers/net/e1000.asm` | 542 | Intel e1000/e1000e NIC driver |
| `syscalls/evolve.asm` | 220 | Evolution benchmarking syscalls |
| `syscalls/ai_scheduler.asm` | 286 | AI-powered SMP scheduler |
| `syscalls/gpu.asm` | 209 | GPU syscall stubs (NO_GPU=ret) |
| `init/gpu.asm` | 60 | GPU init stub (NO_GPU=ret) |
| `sysvar_gpu.asm` | 69 | GPU system variables |
| `drivers/gpu/nvidia.asm` | 952 | RTX 5090 driver (excluded by NO_GPU) |

---

## 4. Hot-Path Deep Analysis

Detailed comparison of the most performance-critical functions.

| Function | Instr (base) | Instr (gen8) | ΔInstr | Bytes (base) | Bytes (gen8) | ΔBytes |
|----------|-------------|-------------|--------|-------------|-------------|--------|
| `ap_clear` | 13 | 10 | -3 | 43 | 40 | -3 |
| `b_smp_lock` | 5 | 7 | +2 | 16 | 18 | +2 |
| `b_smp_unlock` | 2 | 2 | 0 | 6 | 4 | -2 |
| `ap_wakeup` | 9 | 7 | -2 | 21 | 25 | +4 |
| `os_debug_dump_al` | 24 | 20 | -4 | 61 | 64 | +3 |

### Key Optimizations in Hot Paths

#### `ap_clear`

<details><summary>Baseline listing</summary>

```asm
    68                                  ap_clear:				; All cores start here on first start-up and after an exception
    69 000000C0 FA                      	cli				; Disable interrupts on this core
    70                                  
    71                                  	; Get local ID of the core
    72 000000C1 488B342500001100        	mov rsi, [os_LocalAPICAddress]	; We can't use b_smp_get_id as no configured stack yet
    73 000000C9 31C0                    	xor eax, eax			; Clear Task Priority (bits 7:4) and Task Priority Sub-Class (bits 3:0)
    74 000000CB 898680000000            	mov dword [rsi+0x80], eax	; APIC Task Priority Register (TPR)
    75 000000D1 8B4620                  	mov eax, dword [rsi+0x20]	; APIC ID in upper 8 bits
    76 000000D4 C1E818                  	shr eax, 24			; Shift to the right and AL now holds the CPU's APIC ID
    77 000000D7 89C3                    	mov ebx, eax			; Save the APIC ID
    78                                  
    79                                  	; Clear the entry in the work table
    80 000000D9 BF00F81F00              	mov rdi, os_SMP
    81 000000DE 48C1E003                	shl rax, 3			; Quick multiply by 8 to get to proper record
    82 000000E2 4801C7                  	add rdi, rax
    83 000000E5 31C0                    	xor eax, eax
    84 000000E7 0C01                    	or al, 1			; Set bit 0 for "present"
    85 000000E9 48AB                    	stosq				; Clear the code address
    86                                  
```
</details>

<details><summary>Gen-8 listing</summary>

```asm
    83                                  ap_clear:				; All cores start here on first start-up and after an exception
    84 00000120 FA                      	cli				; Disable interrupts on this core
    85                                  
    86                                  	; Get local ID of the core
    87 00000121 488B342500001100        	mov rsi, [os_LocalAPICAddress]	; We can't use b_smp_get_id as no configured stack yet
    88 00000129 31C0                    	xor eax, eax			; Clear Task Priority (bits 7:4) and Task Priority Sub-Class (bits 3:0)
    89 0000012B 898680000000            	mov dword [rsi+0x80], eax	; APIC Task Priority Register (TPR)
    90 00000131 8B4620                  	mov eax, dword [rsi+0x20]	; APIC ID in upper 8 bits
    91 00000134 C1E818                  	shr eax, 24			; Shift to the right and AL now holds the CPU's APIC ID
    92 00000137 89C3                    	mov ebx, eax			; Save the APIC ID
    93                                  
    94                                  	; Clear the entry in the work table
    95 00000139 488D3CC500F81F00        	lea rdi, [os_SMP + rax*8]	; EVOLVED Gen-8: LEA replacing shl+add
    96 00000141 B801000000              	mov eax, 1			; EVOLVED Gen-8: direct mov replacing xor+or
    97 00000146 48AB                    	stosq				; Clear the code address
    98                                  
```
</details>

#### `b_smp_lock`

<details><summary>Baseline listing</summary>

```asm
   268                              <2> b_smp_lock:
   269 00000B56 660FBA2000          <2> 	bt word [rax], 0	; Check if the mutex is free (Bit 0 cleared to 0)
   270 00000B5B 72F9                <2> 	jc b_smp_lock		; If not check it again
   271 00000B5D F0660FBA2800        <2> 	lock bts word [rax], 0	; The mutex was free, lock the bus. Try to grab the mutex
   272 00000B63 72F1                <2> 	jc b_smp_lock		; Jump if we were unsuccessful
   273 00000B65 C3                  <2> 	ret			; Lock acquired. Return to the caller
   274                              <2> ; -----------------------------------------------------------------------------
   275                              <2> 
   276                              <2> 
   277                              <2> ; -----------------------------------------------------------------------------
   278                              <2> ; b_smp_unlock -- Unlock a mutex
   279                              <2> ;  IN:	RAX = Address of lock variable
   280                              <2> ; OUT:	Nothing. All registers preserved.
```
</details>

<details><summary>Gen-8 listing</summary>

```asm
   275                              <2> b_smp_lock:
   276                              <2> b_smp_lock_spin:
   277 00000BF8 F60001              <2> 	test byte [rax], 1	; EVOLVED Gen-8: test replacing bt (shorter, no bus lock)
   278 00000BFB 7404                <2> 	jz b_smp_lock_try	; If free, try to acquire
   279 00000BFD F390                <2> 	pause			; EVOLVED: Hint to CPU we're spin-waiting
   280 00000BFF EBF7                <2> 	jmp b_smp_lock_spin	; Keep spinning (cache line stays Shared)
   281                              <2> b_smp_lock_try:
   282 00000C01 F0660FBA2800        <2> 	lock bts word [rax], 0	; Atomic test-and-set (requires Exclusive cache state)
   283 00000C07 72EF                <2> 	jc b_smp_lock_spin	; Failed: another core grabbed it, back to read-only spin
   284 00000C09 C3                  <2> 	ret			; Lock acquired
   285                              <2> ; -----------------------------------------------------------------------------
   286                              <2> 
   287                              <2> 
   288                              <2> ; -----------------------------------------------------------------------------
   289                              <2> ; b_smp_unlock -- Unlock a mutex
   290                              <2> ;  IN:	RAX = Address of lock variable
   291                              <2> ; OUT:	Nothing. All registers preserved.
```
</details>

#### `b_smp_unlock`

<details><summary>Baseline listing</summary>

```asm
   281                              <2> b_smp_unlock:
   282 00000B66 660FBA3000          <2> 	btr word [rax], 0	; Release the lock (Bit 0 cleared to 0)
   283 00000B6B C3                  <2> 	ret			; Lock released. Return to the caller
   284                              <2> ; -----------------------------------------------------------------------------
   285                              <2> 
   286                              <2> 
   287                              <2> ; =============================================================================
   288                              <2> ; EOF
    15                              <1> %include "syscalls/system.asm"
     1                              <2> ; =============================================================================
     2                              <2> ; AlJefra -- a 64-bit OS written in Assembly for x86-64 systems
     3                              <2> ; Copyright (C) 2008-2026 AlJefra -- see LICENSE.TXT
     4                              <2> ;
     5                              <2> ; System Functions
     6                              <2> ; =============================================================================
     7                              <2> 
     8                              <2> 
     9                              <2> ; -----------------------------------------------------------------------------
    10                              <2> ; b_system - Call system functions
    11                              <2> ; IN:	RCX = Function
    12                              <2> ;	RAX = Variable 1
    13                              <2> ;	RDX = Variable 2
    14                              <2> ; OUT:	RAX = Result
    15                              <2> ;	All other registers preserved
```
</details>

<details><summary>Gen-8 listing</summary>

```asm
   292                              <2> b_smp_unlock:
   293 00000C0A C60000              <2> 	mov byte [rax], 0	; EVOLVED Gen-8: store-release (x86 TSO guarantees visibility)
   294 00000C0D C3                  <2> 	ret			; Lock released. Return to the caller
   295                              <2> ; -----------------------------------------------------------------------------
   296                              <2> 
   297                              <2> 
   298                              <2> ; -----------------------------------------------------------------------------
   299                              <2> ; EVOLVED: Reader-Writer lock primitives
   300                              <2> ; These allow multiple readers OR a single writer
   301                              <2> ; Lock variable format: bits 0-14 = reader count, bit 15 = writer flag
   302                              <2> ; -----------------------------------------------------------------------------
   303                              <2> 
   304                              <2> ; b_smp_read_lock -- Acquire read lock (multiple readers allowed)
   305                              <2> ;  IN:	RAX = Address of lock variable
   306                              <2> ; OUT:	Nothing. All registers preserved.
   307                              <2> ; EVOLVED Gen-6: Fixed correctness bug — cmpxchg needs expected value in AX,
   308                              <2> ; but RAX was the lock address. Move lock address to RDX to free AX for cmpxchg.
```
</details>

#### `ap_wakeup`

<details><summary>Baseline listing</summary>

```asm
    96                              <1> ap_wakeup:
    97 00002790 51                  <1> 	push rcx
    98 00002791 50                  <1> 	push rax
    99                              <1> 
   100                              <1> 	; Acknowledge the IPI
   101 00002792 B9B0000000          <1> 	mov ecx, APIC_EOI
   102 00002797 31C0                <1> 	xor eax, eax
   103 00002799 E87AE6FFFF          <1> 	call os_apic_write
   104                              <1> 
   105 0000279E 58                  <1> 	pop rax
   106 0000279F 59                  <1> 	pop rcx
   107 000027A0 48CF                <1> 	iretq				; Return from the IPI
   108                              <1> ; -----------------------------------------------------------------------------
   109                              <1> 
   110                              <1> 
   111                              <1> ; -----------------------------------------------------------------------------
   112                              <1> ; Resets a CPU to execute ap_clear
   113 000027A2 90<rep 6h>          <1> align 8
```
</details>

<details><summary>Gen-8 listing</summary>

```asm
   166                              <1> ap_wakeup:
   167 00003000 50                  <1> 	push rax
   168                              <1> 	; EVOLVED Gen-8: Inline EOI — eliminates call/ret + push/pop overhead
   169 00003001 488B042500001100    <1> 	mov rax, [os_LocalAPICAddress]
   170 00003009 C780B0000000000000- <1> 	mov dword [rax + APIC_EOI], 0
   170 00003012 00                  <1>
   171 00003013 58                  <1> 	pop rax
   172 00003014 48CF                <1> 	iretq				; Return from the IPI
   173                              <1> ; -----------------------------------------------------------------------------
   174                              <1> 
   175                              <1> 
   176                              <1> ; -----------------------------------------------------------------------------
   177                              <1> ; Resets a CPU to execute ap_clear
   178 00003016 90<rep 2h>          <1> align 8
```
</details>

#### `os_debug_dump_al`

<details><summary>Baseline listing</summary>

```asm
    33                              <2> os_debug_dump_al:
    34 000006D9 50                  <2> 	push rax
    35 000006DA 6650                <2> 	push ax				; Save AX for the low nibble
    36 000006DC C0E804              <2> 	shr al, 4			; Shift the high 4 bits into the low 4, high bits cleared
    37 000006DF 0C30                <2> 	or al, '0'			; Add "0"
    38 000006E1 3C3A                <2> 	cmp al, '9'+1			; Digit?
    39 000006E3 7202                <2> 	jb os_debug_dump_al_h		; Yes, store it
    40 000006E5 0407                <2> 	add al, 7			; Add offset for character "A"
    41                              <2> os_debug_dump_al_h:
    42 000006E7 880425[072C0000]    <2> 	mov [tchar+0], al		; Store first character
    43 000006EE 6658                <2> 	pop ax				; Restore AX
    44 000006F0 240F                <2> 	and al, 0x0F			; Keep only the low 4 bits
    45 000006F2 0C30                <2> 	or al, '0'			; Add "0"
    46 000006F4 3C3A                <2> 	cmp al, '9'+1			; Digit?
    47 000006F6 7202                <2> 	jb os_debug_dump_al_l		; Yes, store it
    48 000006F8 0407                <2> 	add al, 7			; Add offset for character "A"
    49                              <2> os_debug_dump_al_l:
    50 000006FA 880425[082C0000]    <2> 	mov [tchar+1], al		; Store second character
    51 00000701 58                  <2> 	pop rax
    52 00000702 56                  <2> 	push rsi
    53 00000703 51                  <2> 	push rcx
    54 00000704 BE[072C0000]        <2> 	mov esi, tchar
    55 00000709 B902000000          <2> 	mov rcx, 2
    56 0000070E E8C4010000          <2> 	call b_output
    57 00000713 59                  <2> 	pop rcx
    58 00000714 5E                  <2> 	pop rsi
    59 00000715 C3                  <2> 	ret
    60                              <2> ; -----------------------------------------------------------------------------
    61                              <2> 
    62                              <2> 
```
</details>

<details><summary>Gen-8 listing</summary>

```asm
    36                              <2> os_debug_dump_al:
    37 0000076D 50                  <2> 	push rax
    38 0000076E 53                  <2> 	push rbx
    39 0000076F 0FB6D8              <2> 	movzx ebx, al
    40 00000772 C0EB04              <2> 	shr bl, 4			; High nibble index
    41 00000775 8A83[AD070000]      <2> 	mov al, [os_hex_table + rbx]	; EVOLVED: Direct table lookup, no branches
    42 0000077B 880425[87340000]    <2> 	mov [tchar+0], al
    43 00000782 0FB65C2408          <2> 	movzx ebx, byte [rsp+8]	; Reload original AL from stack
    44 00000787 80E30F              <2> 	and bl, 0x0F			; Low nibble index
    45 0000078A 8A83[AD070000]      <2> 	mov al, [os_hex_table + rbx]	; EVOLVED: Direct table lookup, no branches
    46 00000790 880425[88340000]    <2> 	mov [tchar+1], al
    47 00000797 5B                  <2> 	pop rbx
    48 00000798 58                  <2> 	pop rax
    49 00000799 56                  <2> 	push rsi
    50 0000079A 51                  <2> 	push rcx
    51 0000079B BE[87340000]        <2> 	mov esi, tchar
    52 000007A0 B902000000          <2> 	mov rcx, 2
    53 000007A5 E8D6010000          <2> 	call b_output
    54 000007AA 59                  <2> 	pop rcx
    55 000007AB 5E                  <2> 	pop rsi
    56 000007AC C3                  <2> 	ret
    57                              <2> 
    58                              <2> ; EVOLVED: Hex lookup table - eliminates all branch mispredictions in hex dump
```
</details>


---

## 5. Evolution History

| Commit | Generation | Description |
|--------|-----------|-------------|
| `fd5a707` | Baseline | Original AlJefra OS fork |
| `299d302` | Gen-1 | 8 critical kernel optimizations |
| `6d50acc` | Drivers-1 | 8 driver subsystem optimizations |
| `e7829d6` | Gen-6 | 25 optimizations + SMP bug fix |
| `7d0208d` | Gen-7 | 30 optimizations (Experiment B informed) |
| `4c32537` | Gen-8 | 37 optimizations (inline EOI, test/cmp, LEA) |

---

## 6. Methodology

- **Static analysis** via NASM listing files (`-l` flag) — maps every source line to its binary encoding
- **Pattern counting** via grep across shared `.asm` source files
- **Per-component tracking** by parsing `%include` boundaries in NASM listings
- **Fair comparison**: Both kernels built with `-dNO_VGA`; Gen-8 with `-dNO_GPU` to exclude post-fork additions
- **Shared files**: 31 components present in both baseline and Gen-8
- **QEMU timing**: Wall-clock boot to serial output (if bootloader available)

---

*Report generated by `evolution/benchmark_compare.sh`*
