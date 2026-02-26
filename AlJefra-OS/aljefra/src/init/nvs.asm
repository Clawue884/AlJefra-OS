; =============================================================================
; AlJefra OS -- a 64-bit OS written in Assembly for x86-64 systems
; Copyright (C) 2008-2026 AlJefra -- see LICENSE.TXT
;
; Initialize non-volatile storage
; =============================================================================


; -----------------------------------------------------------------------------
; init_nvs -- Configure the first non-volatile storage device it finds
init_nvs:

	; Output progress via serial
	mov esi, msg_nvs
	call os_debug_string

	; Check Bus Table for any other supported controllers
	; EVOLVED: Added prefetch to reduce bus table scan latency
	mov rsi, bus_table		; Load Bus Table address to RSI
	sub rsi, 8			; EVOLVED Gen-10: combined sub 16 + add 8 into single sub 8
init_nvs_check_bus:
	add rsi, 16			; Increment to next record in memory
	prefetchnta [rsi+16]		; EVOLVED: Prefetch next bus table entry
	mov ax, [rsi]			; Load Class Code / Subclass Code
	cmp ax, 0xFFFF			; Check if at end of list
	je init_nvs_done		; No storage controller found
	cmp ax, 0x0100			; Mass Storage Controller (01) / SCSI storage controller (00)
	je init_nvs_virtio_blk
	jmp init_nvs_check_bus		; Check Bus Table again

init_nvs_virtio_blk:
	sub rsi, 8			; Move RSI back to start of Bus record
	mov edx, [rsi]			; Load value for os_bus_read/write
	call virtio_blk_init
	jmp init_nvs_done

init_nvs_done:

%ifndef NO_LFB
	; Output block to screen (5/8)
	mov ebx, 8
	call os_debug_block
%endif

	; Output progress via serial
	mov esi, msg_ok
	call os_debug_string

	ret
; -----------------------------------------------------------------------------


; =============================================================================
; EOF
