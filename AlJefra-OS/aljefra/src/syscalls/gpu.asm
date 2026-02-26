; =============================================================================
; AlJefra OS -- a 64-bit OS written in Assembly for x86-64 systems
; Copyright (C) 2008-2026 AlJefra -- see LICENSE.TXT
;
; GPU Syscalls -- User-accessible GPU compute and memory functions
; =============================================================================


%ifndef NO_GPU

; -----------------------------------------------------------------------------
; b_gpu_status -- Get GPU status
;  IN:	Nothing
; OUT:	RAX = Status word (see gpu_get_status)
b_gpu_status:
	jmp gpu_get_status		; EVOLVED Gen-10: tail-call
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; b_gpu_compute -- Dispatch a compute workload to the GPU
;  IN:	RAX = Pointer to compute parameters structure
; OUT:	RAX = Fence ID (0xFFFFFFFF on failure)
b_gpu_compute:
	push rsi
	mov rsi, rax
	call gpu_compute_dispatch
	jnc b_gpu_compute_done
	mov eax, 0xFFFFFFFF		; Failure
b_gpu_compute_done:
	pop rsi
	ret
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; b_gpu_mem_alloc -- Allocate GPU VRAM
;  IN:	RCX = Size in bytes (rounded up to 2MB pages)
; OUT:	RAX = VRAM offset (0xFFFFFFFFFFFFFFFF on failure)
b_gpu_mem_alloc:
	push rcx

	; Convert bytes to pages (round up)
	add rcx, GPU_VRAM_PAGE_SIZE - 1
	shr rcx, GPU_VRAM_PAGE_SHIFT
	call gpu_vram_alloc

	pop rcx
	ret
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; b_gpu_mem_free -- Free GPU VRAM
;  IN:	RAX = VRAM offset (as returned by b_gpu_mem_alloc)
;	RCX = Size in bytes (same as was allocated)
; OUT:	Nothing
b_gpu_mem_free:
	push rcx

	add rcx, GPU_VRAM_PAGE_SIZE - 1
	shr rcx, GPU_VRAM_PAGE_SHIFT
	call gpu_vram_free

	pop rcx
	ret
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; b_gpu_mem_copy_to -- DMA copy system memory to VRAM
;  IN:	RSI = Source in system memory
;	RDI = Destination offset in VRAM
;	RCX = Byte count
; OUT:	RAX = Fence ID
b_gpu_mem_copy_to:
	jmp gpu_dma_copy_to_vram	; EVOLVED Gen-10: tail-call
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; b_gpu_mem_copy_from -- DMA copy VRAM to system memory
;  IN:	RSI = Source offset in VRAM
;	RDI = Destination in system memory
;	RCX = Byte count
; OUT:	RAX = Fence ID
b_gpu_mem_copy_from:
	jmp gpu_dma_copy_from_vram	; EVOLVED Gen-10: tail-call
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; b_gpu_fence_wait -- Wait for a GPU operation to complete
;  IN:	RAX = Fence ID
; OUT:	Nothing
b_gpu_fence_wait:
	jmp gpu_fence_wait		; EVOLVED Gen-10: tail-call
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; b_gpu_mmio_read -- Direct MMIO register read (for advanced users)
;  IN:	ECX = Register offset
; OUT:	EAX = Register value
b_gpu_mmio_read:
	jmp gpu_mmio_read32		; EVOLVED Gen-10: tail-call
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; b_gpu_mmio_write -- Direct MMIO register write (for advanced users)
;  IN:	ECX = Register offset
;	EAX = Value to write
; OUT:	Nothing
b_gpu_mmio_write:
	jmp gpu_mmio_write32		; EVOLVED Gen-10: tail-call
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; b_gpu_vram_info -- Get VRAM information
;  IN:	Nothing
; OUT:	RAX = Total VRAM in bytes
;	RDX = Free VRAM in bytes
b_gpu_vram_info:
	push rcx

	; Total VRAM
	mov eax, [os_GPU_VRAM_MiB]	; EVOLVED Gen-10: removed dead xor (mov eax zero-extends)
	shl rax, 20			; Convert MiB to bytes

	; Free VRAM
	push rax
	call gpu_get_vram_free
	mov rdx, rax
	pop rax

	pop rcx
	ret
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; b_gpu_benchmark -- Run GPU benchmark
;  IN:	Nothing
; OUT:	RAX = Average command latency in timer ticks
b_gpu_benchmark:
	jmp gpu_benchmark		; EVOLVED Gen-10: tail-call
; -----------------------------------------------------------------------------


%else
; NO_GPU stubs -- return failure/zero for all GPU syscalls

b_gpu_status:
	xor eax, eax
	ret

b_gpu_compute:
	mov eax, 0xFFFFFFFF
	ret

b_gpu_mem_alloc:
	mov rax, 0xFFFFFFFFFFFFFFFF
	ret

b_gpu_mem_free:
	ret

b_gpu_mem_copy_to:
	xor eax, eax
	ret

b_gpu_mem_copy_from:
	xor eax, eax
	ret

b_gpu_fence_wait:
	ret

b_gpu_mmio_read:
	xor eax, eax
	ret

b_gpu_mmio_write:
	ret

b_gpu_vram_info:
	xor eax, eax
	xor edx, edx
	ret

b_gpu_benchmark:
	xor eax, eax
	ret

%endif


; =============================================================================
; EOF
