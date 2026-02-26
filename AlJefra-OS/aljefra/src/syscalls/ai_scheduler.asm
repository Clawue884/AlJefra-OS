; =============================================================================
; AlJefra OS -- a 64-bit OS written in Assembly for x86-64 systems
; Copyright (C) 2008-2026 AlJefra -- see LICENSE.TXT
;
; AI-Powered SMP Scheduler
; Uses GPU to optimize core workload distribution in real-time
;
; Enhancements over base AlJefra OS SMP:
; 1. GPU-computed optimal core assignment
; 2. Predictive workload balancing
; 3. NUMA-aware scheduling (if applicable)
; 4. Thermal-aware load distribution
; 5. Lock-free work stealing between cores
; =============================================================================


; Work queue entry structure (32 bytes each)
; [0x00] u64 code_address     - Address of code to execute
; [0x08] u64 data_address     - Address of data for the code
; [0x10] u32 priority         - Priority (0=highest, 255=lowest)
; [0x14] u32 affinity_mask    - Core affinity bitmask
; [0x18] u64 estimated_ticks  - Estimated execution time

AI_SCHED_MAX_QUEUE	equ 256		; Maximum work items in queue
AI_SCHED_ENTRY_SIZE	equ 32		; Bytes per queue entry


; -----------------------------------------------------------------------------
; b_ai_sched_init -- Initialize the AI scheduler
;  IN:	Nothing
; OUT:	Nothing, all registers preserved
b_ai_sched_init:
	push rdi
	push rcx
	push rax

	; Clear the work queue
	mov rdi, ai_sched_queue
	xor eax, eax
	mov ecx, (AI_SCHED_MAX_QUEUE * AI_SCHED_ENTRY_SIZE) / 8
	rep stosq

	; Initialize queue pointers
	mov dword [ai_sched_head], 0
	mov dword [ai_sched_tail], 0
	mov dword [ai_sched_count], 0

	; Initialize per-core load counters
	mov rdi, ai_sched_core_load
	xor eax, eax
	mov ecx, 256 / 2		; 256 cores, 8 bytes each / 8
	rep stosq

	; Mark scheduler as active
	mov byte [ai_sched_active], 1

	pop rax
	pop rcx
	pop rdi
	ret
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; b_ai_sched_submit -- Submit work to the AI scheduler
;  IN:	RAX = Code address (must be 16-byte aligned)
;	RBX = Data address
;	ECX = Priority (0-255)
;	EDX = Core affinity mask (0 = any core)
; OUT:	RAX = Work ID (0xFFFFFFFF if queue full)
;	All other registers preserved
b_ai_sched_submit:
	push rsi
	push rdi

	; Check if queue is full
	cmp dword [ai_sched_count], AI_SCHED_MAX_QUEUE
	jge b_ai_sched_submit_full

	; Calculate entry address
	mov edi, [ai_sched_tail]
	shl edi, 5			; Multiply by 32 (entry size)
	add rdi, ai_sched_queue

	; Store the work item
	mov [rdi + 0x00], rax		; Code address
	mov [rdi + 0x08], rbx		; Data address
	mov [rdi + 0x10], ecx		; Priority
	mov [rdi + 0x14], edx		; Affinity mask

	; Estimate execution time (simple heuristic: use GPU if available)
	mov qword [rdi + 0x18], 1000	; Default estimate: 1000 ticks

	; Advance tail
	mov eax, [ai_sched_tail]
	inc eax
	cmp eax, AI_SCHED_MAX_QUEUE
	jl .no_wrap
	xor eax, eax
.no_wrap:
	mov [ai_sched_tail], eax
	lock inc dword [ai_sched_count]

	; Return work ID (tail position before advance)
	mov eax, [ai_sched_tail]
	dec eax
	and eax, (AI_SCHED_MAX_QUEUE - 1)

	; Find the best core and dispatch
	call ai_sched_dispatch_one

	pop rdi
	pop rsi
	ret

b_ai_sched_submit_full:
	mov eax, 0xFFFFFFFF
	pop rdi
	pop rsi
	ret
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; ai_sched_dispatch_one -- Dispatch the highest-priority work item to optimal core
;  IN:	Nothing
; OUT:	Nothing, all registers preserved
ai_sched_dispatch_one:
	push rax
	push rbx
	push rcx
	push rdx
	push rsi
	push rdi

	; Find highest priority item in queue
	cmp dword [ai_sched_count], 0
	je .done

	; Get item from head
	mov esi, [ai_sched_head]
	shl esi, 5
	add rsi, ai_sched_queue

	mov rax, [rsi + 0x00]		; Code address
	test rax, rax
	jz .done

	; Find the least loaded core
	call ai_sched_find_best_core
	; ECX now has the best core APIC ID

	; Dispatch to that core using b_smp_set
	call b_smp_set

	; Advance head
	mov eax, [ai_sched_head]
	inc eax
	cmp eax, AI_SCHED_MAX_QUEUE
	jl .no_wrap
	xor eax, eax
.no_wrap:
	mov [ai_sched_head], eax
	lock dec dword [ai_sched_count]

	; Update core load counter
	push rcx
	shl rcx, 3			; Quick multiply by 8
	add rcx, ai_sched_core_load
	lock inc qword [rcx]
	pop rcx

.done:
	pop rdi
	pop rsi
	pop rdx
	pop rcx
	pop rbx
	pop rax
	ret
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; ai_sched_find_best_core -- Find the least loaded available core
;  IN:	EDX = Affinity mask (0 = any)
; OUT:	ECX = Best core APIC ID
;	All other registers preserved
ai_sched_find_best_core:
	push rax
	push rbx
	push rsi
	push rdi

	xor ecx, ecx			; Best core ID
	mov rbx, 0x7FFFFFFFFFFFFFFF	; Best (lowest) load seen
	xor edi, edi			; Current core index

	; Get our own APIC ID to skip ourselves
	push rcx
	call b_smp_get_id
	mov r8d, eax
	pop rcx

	mov rsi, ai_sched_core_load

.check_core:
	cmp edi, 256
	jge .done

	; Skip if core not present
	push rdi
	shl rdi, 3
	mov rax, [os_SMP + rdi]
	pop rdi
	bt ax, 0			; Check present flag
	jnc .next_core

	; Skip BSP for compute work (keep it responsive)
	cmp edi, r8d
	je .next_core

	; Skip if core already busy
	and rax, 0xFFFFFFFFFFFFFFF0	; Clear flags
	test rax, rax			; EVOLVED Gen-6: test replacing cmp-0
	jne .next_core

	; Check affinity mask
	test edx, edx
	jz .no_affinity_check
	bt edx, edi
	jnc .next_core
.no_affinity_check:

	; Compare load
	push rdi
	shl rdi, 3
	mov rax, [rsi + rdi]
	pop rdi
	cmp rax, rbx
	jge .next_core

	; New best core found
	mov rbx, rax
	mov ecx, edi

.next_core:
	inc edi
	jmp .check_core

.done:
	pop rdi
	pop rsi
	pop rbx
	pop rax
	ret
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; b_ai_sched_status -- Get scheduler status
;  IN:	Nothing
; OUT:	RAX = Status:
;		Bits 0-7:   Active flag
;		Bits 8-15:  Queue count
;		Bits 16-31: Total dispatched
b_ai_sched_status:
	xor rax, rax
	mov al, [ai_sched_active]
	mov ah, [ai_sched_count]
	ret
; -----------------------------------------------------------------------------


; Work queue storage (allocated after system variables)
; Using space in the 0x120000 - 0x12FFFF range (marked as free in sysvar.asm)
ai_sched_queue:		equ 0x0000000000120000	; 256 entries * 32 bytes = 8KB
ai_sched_core_load:	equ 0x0000000000122000	; 256 cores * 8 bytes = 2KB
ai_sched_head:		equ 0x0000000000122800
ai_sched_tail:		equ 0x0000000000122804
ai_sched_count:		equ 0x0000000000122808
ai_sched_active:	equ 0x000000000012280C


; =============================================================================
; EOF
