; =============================================================================
; AlJefra OS -- a 64-bit OS written in Assembly for x86-64 systems
; Copyright (C) 2008-2026 AlJefra -- see LICENSE.TXT
;
; The AlJefra OS exokernel
; =============================================================================


BITS 64					; Specify 64-bit
ORG 0x0000000000100000			; The kernel needs to be loaded at this address
DEFAULT ABS

%DEFINE ALJEFRA_VER 'AlJefra OS v1.0.0 (GPU-Evolved)', 13, 'x86-64 Exokernel | GPU Engine: RTX 5090', 13, 0
%DEFINE ALJEFRA_API_VER 1
%ifdef NO_GPU
KERNELSIZE equ 20 * 1024		; Standard kernel size without GPU
%else
KERNELSIZE equ 64 * 1024		; Expanded for GPU engine
%endif


kernel_start:
	jmp start			; Skip over the function call index
	nop
	db 'ALJEFRA'			; Kernel signature

align 16
	dq b_input			; 0x0010
	dq b_output			; 0x0018
	dq b_net_tx			; 0x0020
	dq b_net_rx			; 0x0028
	dq b_nvs_read			; 0x0030
	dq b_nvs_write			; 0x0038
	dq b_system			; 0x0040
	dq b_user			; 0x0048
	dq b_gpu_status			; 0x0050 - GPU status
	dq b_gpu_compute		; 0x0058 - GPU compute dispatch
	dq b_gpu_mem_alloc		; 0x0060 - GPU VRAM allocate
	dq b_gpu_mem_free		; 0x0068 - GPU VRAM free
	dq b_gpu_mem_copy_to		; 0x0070 - DMA to VRAM
	dq b_gpu_mem_copy_from		; 0x0078 - DMA from VRAM
	dq b_gpu_fence_wait		; 0x0080 - GPU fence wait
	dq b_gpu_mmio_read		; 0x0088 - GPU MMIO read
	dq b_gpu_mmio_write		; 0x0090 - GPU MMIO write
	dq b_gpu_vram_info		; 0x0098 - GPU VRAM info
	dq b_gpu_benchmark		; 0x00A0 - GPU benchmark

align 16
start:
	mov rsp, 0x10000		; Set the temporary stack

	; System and driver initialization
	call init_64			; After this point we are in a working 64-bit environment
	call init_bus			; Initialize system busses
%ifndef NO_GPU
	call init_gpu			; Initialize GPU (NVIDIA RTX 5090)
%endif
	call init_nvs			; Initialize non-volatile storage
	call init_net			; Initialize network
	call init_hid			; Initialize human interface devices
	call init_sys			; Initialize system

	; Set the payload to run
start_payload:
	cmp byte [os_payload], 0
	je ap_clear			; If no payload was present then skip to ap_clear
	mov rsi, [os_LocalAPICAddress]	; We can't use b_smp_get_id as no configured stack yet
	xor eax, eax			; Clear Task Priority (bits 7:4) and Task Priority Sub-Class (bits 3:0)
	mov dword [rsi+0x80], eax	; APIC Task Priority Register (TPR)
	mov eax, dword [rsi+0x20]	; APIC ID in upper 8 bits
	shr eax, 24			; Shift to the right and AL now holds the CPU's APIC ID
	mov [os_BSP], al		; Keep a record of the BSP APIC ID
	mov ebx, eax			; Save the APIC ID
	lea rdi, [os_SMP + rax*8]	; EVOLVED Gen-8: LEA replacing shl+add
	mov eax, 1			; EVOLVED Gen-8: direct mov replacing xor+or
	stosq				; Clear the code address
	mov rcx, rbx			; Copy the APIC ID for b_smp_set
	mov rax, 0x1E0000		; Payload was copied here
	call b_smp_set
	jmp bsp				; Skip to bsp as payload was prepped

align 16
ap_clear:				; All cores start here on first start-up and after an exception
	cli				; Disable interrupts on this core

	; Get local ID of the core
	mov rsi, [os_LocalAPICAddress]	; We can't use b_smp_get_id as no configured stack yet
	xor eax, eax			; Clear Task Priority (bits 7:4) and Task Priority Sub-Class (bits 3:0)
	mov dword [rsi+0x80], eax	; APIC Task Priority Register (TPR)
	mov eax, dword [rsi+0x20]	; APIC ID in upper 8 bits
	shr eax, 24			; Shift to the right and AL now holds the CPU's APIC ID
	mov ebx, eax			; Save the APIC ID

	; Clear the entry in the work table
	lea rdi, [os_SMP + rax*8]	; EVOLVED Gen-8: LEA replacing shl+add
	mov eax, 1			; EVOLVED Gen-8: direct mov replacing xor+or
	stosq				; Clear the code address

bsp:
	; Set up the stack
	mov eax, ebx			; Restore the APIC ID
	shl rax, 16			; Shift left 16 bits for an 64 KiB stack
	add rax, [os_StackBase]		; The stack decrements when you "push", start at 64 KiB in
	add rax, 65536			; 64 KiB Stack
	mov rsp, rax

	; Clear registers. Gives us a clean slate to work with
	xor eax, eax			; aka r0
	xor ecx, ecx			; aka r1
	xor edx, edx			; aka r2
	xor ebx, ebx			; aka r3
	xor ebp, ebp			; aka r5, We skip RSP (aka r4) as it was previously set
	xor esi, esi			; aka r6
	xor edi, edi			; aka r7
	xor r8, r8
	xor r9, r9
	xor r10, r10
	xor r11, r11
	xor r12, r12
	xor r13, r13
	xor r14, r14
	xor r15, r15
	sti				; Enable interrupts on this core

ap_check:
	call b_smp_get			; Check for an assigned workload
	and rax, -16			; Clear low 4 bits + set ZF on full 64-bit result
	jnz ap_process

	; EVOLVED: Before halting, check the AI scheduler work queue
	; This enables work-stealing: idle cores pull work from the shared queue
	cmp dword [ai_sched_count], 0
	jne ap_check_scheduler

ap_halt:				; Halt until a wakeup call is received
	hlt
	jmp ap_check			; Core will jump to ap_check when it wakes up

ap_check_scheduler:
	; EVOLVED: Try to pull work from the AI scheduler queue
	; This is the work-stealing path - idle cores become productive
	call ai_sched_dispatch_one
	jmp ap_check			; Re-check after dispatch attempt

ap_process:
	mov rcx, 1			; Set the active flag
	call b_smp_setflag
	xor ecx, ecx
	call rax			; Run the code
	jmp ap_clear			; Reset the stack, clear the registers, and wait for something else to work on

; Includes
%include "init.asm"
%include "syscalls.asm"
%include "drivers.asm"
%include "interrupt.asm"
%include "sysvar.asm"			; Include this last to keep the read/write variables away from the code
%include "sysvar_gpu.asm"		; GPU and Evolution engine system variables

EOF:
	db 0xDE, 0xAD, 0xC0, 0xDE

times KERNELSIZE-($-$$) db 0x90		; Set the compiled kernel binary to at least this size in bytes


; =============================================================================
; EOF
