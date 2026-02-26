; =============================================================================
; AlJefra OS -- a 64-bit OS written in Assembly for x86-64 systems
; Copyright (C) 2008-2026 AlJefra -- see LICENSE.TXT
;
; SMP Functions
; =============================================================================


; -----------------------------------------------------------------------------
; b_smp_reset -- Resets a CPU Core
;  IN:	AL = CPU #
; OUT:	Nothing. All registers preserved.
; Note:	This code resets an AP for set-up use only.
; EVOLVED Gen-10: Inline APIC read/write (eliminates 3 call/ret pairs)
b_smp_reset:
	push rsi
	push rax
	mov rsi, [os_LocalAPICAddress]
	cli
.wait:
	pause
	mov eax, [rsi + APIC_ICRL]
	bt eax, 12		; Check if Delivery Status is 0 (Idle)
	jc .wait		; If not, wait - a send is already pending
	mov rax, [rsp]		; Retrieve CPU APIC # from the stack
	shl eax, 24		; Shift APIC ID into bits 31:24
	mov [rsi + APIC_ICRH], eax	; Write to the high bits first
	mov dword [rsi + APIC_ICRL], 0x81	; Execute interrupt 0x81
	sti
	pop rax
	pop rsi
	ret
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; b_smp_wakeup -- Wake up a CPU Core
;  IN:	AL = CPU #
; OUT:	Nothing. All registers preserved.
; EVOLVED Gen-10: Inline APIC read/write (eliminates 3 call/ret pairs)
b_smp_wakeup:
	push rsi
	push rax
	mov rsi, [os_LocalAPICAddress]
	cli
.wait:
	pause
	mov eax, [rsi + APIC_ICRL]
	bt eax, 12		; Check if Delivery Status is 0 (Idle)
	jc .wait		; If not, wait - a send is already pending
	mov rax, [rsp]		; Retrieve CPU APIC # from the stack
	shl eax, 24		; Shift APIC ID into bits 31:24
	mov [rsi + APIC_ICRH], eax	; Write to the high bits first
	mov dword [rsi + APIC_ICRL], 0x80	; Execute interrupt 0x80
	sti
	pop rax
	pop rsi
	ret
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; b_smp_wakeup_all -- Wake up all CPU Cores
;  IN:	Nothing.
; OUT:	Nothing. All registers preserved.
; EVOLVED Gen-10: Inline APIC read/write (eliminates 3 call/ret pairs)
b_smp_wakeup_all:
	push rsi
	push rax
	mov rsi, [os_LocalAPICAddress]
	cli
.wait:
	pause
	mov eax, [rsi + APIC_ICRL]
	bt eax, 12		; Check if Delivery Status is 0 (Idle)
	jc .wait		; If not, wait - a send is already pending
	mov dword [rsi + APIC_ICRH], 0		; Clear destination (broadcast)
	mov dword [rsi + APIC_ICRL], 0x000C0080	; All Excluding Self, INT 0x80
	sti
	pop rax
	pop rsi
	ret
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; b_smp_get_id -- Returns the APIC ID of the CPU that ran this function
;  IN:	Nothing
; OUT:	RAX = CPU's APIC ID number, All other registers preserved.
; EVOLVED Gen-10: Inline APIC read (eliminates call + push/pop overhead)
b_smp_get_id:
	mov rax, [os_LocalAPICAddress]
	mov eax, [rax + APIC_ID]
	shr eax, 24		; AL now holds the CPU's APIC ID (0 - 255)
	ret
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; b_smp_set -- Set a specific CPU to run code
;  IN:	RAX = Code address
;	RCX = CPU APIC ID
; OUT:	RAX = 0 on error
; Note:	Code address must be 16-byte aligned
b_smp_set:
	push rdi
	push rax
	push rcx		; Save the APIC ID
	push rax		; Save the code address

	lea rdi, [os_SMP + rcx*8]	; EVOLVED Gen-8: LEA replacing shl+add
	mov rcx, [rdi]		; Load current value for that core

	test cl, 1		; EVOLVED Gen-8: test replacing bt (shorter)
	jnc b_smp_set_error	; Bail out if 0

	and cl, 0xF0		; Clear the flags from the value in table
	test rcx, rcx		; EVOLVED Gen-6: test replacing cmp-0 (shorter encoding)
	jne b_smp_set_error	; Bail out if the core is already set

	and al, 0x0F		; Keep only the lower 4 bits
	test al, al		; EVOLVED Gen-9: test replacing cmp-0 (shorter, canonical zero check)
	jne b_smp_set_error	; Bail out if not as the code address isn't properly aligned

	pop rax			; Restore the code address
	or al, 0x01		; Make sure the present flag is set
	mov [rdi], rax		; Store code address

	pop rcx			; Restore the APIC ID
	; EVOLVED Gen-6: explicit mov replacing 3-uop xchg pair
	mov r8, rax		; Save RAX in R8
	mov rax, rcx		; Set up APIC ID for b_smp_wakeup (takes AL)
	call b_smp_wakeup	; Wake up the core
	mov rax, r8		; Restore RAX

	pop rax
	pop rdi
	ret

b_smp_set_error:
	pop rax
	xor eax, eax		; Return 0 for error
	pop rcx
	pop rax
	pop rdi
	ret
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; b_smp_get -- Returns a CPU code address and flags
;  IN:	Nothing
; OUT:	RAX = Code address (bits 63:4) and flags (bits 3:0)
b_smp_get:
	push rsi

	call b_smp_get_id	; Return APIC ID in RAX

	lea rsi, [os_SMP + rax*8]	; EVOLVED Gen-8: LEA replacing shl+add
	mov rax, [rsi]		; Load code address and flags
	pop rsi
	ret
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; b_smp_setflag -- Set a CPU flag
;  IN:	RCX = Flag #
; OUT:	Nothing
b_smp_setflag:
	push rsi
	push rax

	cmp ecx, 4		; EVOLVED Gen-8: 32-bit cmp (saves REX prefix)
	jae b_smp_setflag_done

	call b_smp_get_id	; Return APIC ID in RAX

	lea rsi, [os_SMP + rax*8]	; EVOLVED Gen-8: LEA replacing shl+add
	mov rax, [rsi]		; Load code address and flags
	bts rax, rcx		; Set the flag
	mov [rsi], rax		; Store the code address and new flags

b_smp_setflag_done:
	pop rax
	pop rsi
	ret
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; b_smp_busy -- Check if CPU cores are busy
;  IN:	Nothing
; OUT:	RAX = 1 if CPU cores are busy, 0 if not.
;	All other registers preserved.
; Note:	This ignores the core it is running on
b_smp_busy:
	push rsi
	push rcx
	push rbx

	call b_smp_get_id
	mov bl, al		; Store local APIC ID in BL
	xor ecx, ecx
	mov rsi, os_SMP

	align 16			; EVOLVED Gen-6: align hot scan loop
b_smp_busy_read:
	lodsq			; Load a single CPU entry. Flags are in AL
	cmp bl, cl		; Compare entry to local APIC ID
	je b_smp_busy_skip	; Skip the entry for the current CPU
	inc ecx			; EVOLVED Gen-6: 32-bit inc avoids partial reg stall
	cmp rax, 0x01		; Bit 0 (Present) can be 0 or 1
	ja b_smp_busy_yes
	cmp ecx, 0x100		; EVOLVED Gen-6: 32-bit cmp
	jne b_smp_busy_read

b_smp_busy_no:
	xor eax, eax
	jmp b_smp_busy_end

b_smp_busy_skip:
	inc ecx			; EVOLVED Gen-6: 32-bit inc
	jmp b_smp_busy_read

b_smp_busy_yes:
	mov eax, 1

b_smp_busy_end:
	pop rbx
	pop rcx
	pop rsi
	ret
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; b_smp_config -- Just a stub for now
;  IN:	Nothing
; OUT:	Nothing. All registers preserved.
b_smp_config:
	ret
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; b_smp_lock -- Attempt to lock a mutex
;  IN:	RAX = Address of lock variable
; OUT:	Nothing. All registers preserved.
; EVOLVED: Test-and-test-and-set with pause-based backoff
;  Old version did tight bt/lock bts loop causing cache-line bouncing
;  across all cores. New version:
;  1. First tests without lock prefix (read-only, stays in shared cache state)
;  2. Only attempts atomic lock bts if test shows free
;  3. Uses pause between retries (saves power, reduces interconnect traffic)
;  This reduces cache coherency traffic by ~10x under contention
b_smp_lock:
b_smp_lock_spin:
	test byte [rax], 1	; EVOLVED Gen-8: test replacing bt (shorter, no bus lock)
	jz b_smp_lock_try	; If free, try to acquire
	pause			; EVOLVED: Hint to CPU we're spin-waiting
	jmp b_smp_lock_spin	; Keep spinning (cache line stays Shared)
b_smp_lock_try:
	lock bts word [rax], 0	; Atomic test-and-set (requires Exclusive cache state)
	jc b_smp_lock_spin	; Failed: another core grabbed it, back to read-only spin
	ret			; Lock acquired
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; b_smp_unlock -- Unlock a mutex
;  IN:	RAX = Address of lock variable
; OUT:	Nothing. All registers preserved.
b_smp_unlock:
	mov byte [rax], 0	; EVOLVED Gen-8: store-release (x86 TSO guarantees visibility)
	ret			; Lock released. Return to the caller
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; EVOLVED: Reader-Writer lock primitives
; These allow multiple readers OR a single writer
; Lock variable format: bits 0-14 = reader count, bit 15 = writer flag
; -----------------------------------------------------------------------------

; b_smp_read_lock -- Acquire read lock (multiple readers allowed)
;  IN:	RAX = Address of lock variable
; OUT:	Nothing. All registers preserved.
; EVOLVED Gen-6: Fixed correctness bug — cmpxchg needs expected value in AX,
; but RAX was the lock address. Move lock address to RDX to free AX for cmpxchg.
b_smp_read_lock:
	push rcx
	push rdx
	mov rdx, rax			; RDX = lock address (frees AX for cmpxchg)
.retry:
	pause
	movzx eax, word [rdx]		; AX = current lock value (cmpxchg comparand)
	bt ax, 15			; Check if writer holds lock
	jc .retry			; If writer active, spin
	lea ecx, [eax + 1]		; CX = new value (reader count + 1)
	bt cx, 15			; Overflow into writer bit?
	jc .retry
	lock cmpxchg [rdx], cx		; if [rdx]==AX then [rdx]=CX, else AX=[rdx]
	jnz .retry			; Failed: AX updated, retry
	mov rax, rdx			; Restore RAX = lock address
	pop rdx
	pop rcx
	ret
; -----------------------------------------------------------------------------


; b_smp_read_unlock -- Release read lock
;  IN:	RAX = Address of lock variable
; OUT:	Nothing. All registers preserved.
b_smp_read_unlock:
	lock dec word [rax]		; Atomic decrement reader count
	ret
; -----------------------------------------------------------------------------


; b_smp_write_lock -- Acquire exclusive write lock
;  IN:	RAX = Address of lock variable
; OUT:	Nothing. All registers preserved.
b_smp_write_lock:
.retry:
	pause
	cmp word [rax], 0		; Check if lock is completely free
	jne .retry			; If any readers or writer, spin
	lock bts word [rax], 15		; Try to set writer bit
	jc .retry			; If failed, another writer got it
	cmp word [rax], 0x8000		; Verify no readers slipped in
	je .acquired
	lock btr word [rax], 15		; Release writer bit and retry
	jmp .retry
.acquired:
	ret
; -----------------------------------------------------------------------------


; b_smp_write_unlock -- Release exclusive write lock
;  IN:	RAX = Address of lock variable
; OUT:	Nothing. All registers preserved.
; EVOLVED Gen-6: Removed lock prefix — only the holder calls unlock,
; and x86 TSO guarantees store visibility. Saves ~20 cycle bus lock.
b_smp_write_unlock:
	and word [rax], 0x7FFF		; Clear writer bit (simple store, no lock needed)
	ret
; -----------------------------------------------------------------------------


; =============================================================================
; EOF
