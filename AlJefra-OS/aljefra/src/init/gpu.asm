; =============================================================================
; AlJefra OS -- a 64-bit OS written in Assembly for x86-64 systems
; Copyright (C) 2008-2026 AlJefra -- see LICENSE.TXT
;
; Initialize GPU
; =============================================================================


; -----------------------------------------------------------------------------
%ifndef NO_GPU
init_gpu:
	; Output progress via serial
	mov esi, msg_gpu_init
	call os_debug_string

	; Set default GPU memory regions
	; Command queue at 0x400000 (4MB mark, 64KB)
	mov qword [os_GPU_CmdQ_Base], 0x0000000000400000

	; Initialize the NVIDIA GPU driver
	call gpu_init
	jc init_gpu_done		; EVOLVED Gen-10: merged duplicate exit paths

	; Output GPU info via serial
	mov esi, msg_gpu_vram
	call os_debug_string
	mov eax, [os_GPU_VRAM_MiB]
	call os_debug_dump_eax
	mov esi, msg_gpu_mb
	call os_debug_string

	; Output chip ID
	mov esi, msg_gpu_chip
	call os_debug_string
	mov eax, [os_GPU_ChipID]
	call os_debug_dump_eax

init_gpu_done:
	mov esi, msg_ok
	call os_debug_string
	ret

; Strings
msg_gpu_init:	db 13, 10, 'gpu', 0
msg_gpu_vram:	db ' vram:', 0
msg_gpu_mb:	db 'MB', 0
msg_gpu_chip:	db ' chip:', 0

%else
init_gpu:
	ret
%endif


; =============================================================================
; EOF
