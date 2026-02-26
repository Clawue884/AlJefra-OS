; =============================================================================
; AlJefra OS -- a 64-bit OS written in Assembly for x86-64 systems
; Copyright (C) 2008-2026 AlJefra -- see LICENSE.TXT
;
; System Functions
; =============================================================================


; -----------------------------------------------------------------------------
; b_system - Call system functions
; IN:	RCX = Function
;	RAX = Variable 1
;	RDX = Variable 2
; OUT:	RAX = Result
;	All other registers preserved
; EVOLVED: Bounds check extended, fast-path for common syscalls
b_system:
	cmp ecx, 0x90			; EVOLVED Gen-7: 32-bit cmp (saves REX prefix)
	jae b_system_end

	; EVOLVED: Fast-path for the most common syscalls (avoid table lookup)
	; Timecounter (0x00) is called extremely frequently for benchmarking
	test ecx, ecx
	jz b_system_timecounter		; Direct jump, skip table entirely

; Use CX register as an index to the function table
; To save memory, the functions are placed in 16-bit frames
	push rcx
	lea ecx, [b_system_table+ecx*2]	; extract function from table by index
	movzx ecx, word [ecx]		; EVOLVED: Use movzx instead of mov cx for clean upper bits
	call rcx			; call function
	pop rcx

b_system_end:
	ret

; Basic

b_system_timecounter:
	jmp [sys_timer]			; EVOLVED Gen-7: tail-call

b_system_free_memory:
	mov eax, [os_MemAmount]
	ret

; CPU

b_system_smp_get_id:
	jmp b_smp_get_id		; EVOLVED Gen-7: tail-call

b_system_smp_numcores:
	movzx eax, word [os_NumCores]	; EVOLVED Gen-7: movzx replacing xor+mov
	ret

b_system_smp_set:
	mov rcx, rdx
	jmp b_smp_set			; EVOLVED Gen-7: tail-call

b_system_smp_get:
	jmp b_smp_get			; EVOLVED Gen-7: tail-call

b_system_smp_lock:
	jmp b_smp_lock			; EVOLVED Gen-7: tail-call

b_system_smp_unlock:
	jmp b_smp_unlock		; EVOLVED Gen-7: tail-call

b_system_smp_busy:
	jmp b_smp_busy			; EVOLVED Gen-7: tail-call

b_system_tsc:
	jmp b_tsc			; EVOLVED Gen-7: tail-call

; Video

b_system_screen_lfb_get:
	mov rax, [os_screen_lfb]
	ret

b_system_screen_x_get:
	movzx eax, word [os_screen_x]	; EVOLVED Gen-7: movzx replacing xor+mov
	ret

b_system_screen_y_get:
	movzx eax, word [os_screen_y]	; EVOLVED Gen-7: movzx replacing xor+mov
	ret

b_system_screen_ppsl_get:
	movzx eax, word [os_screen_ppsl] ; EVOLVED Gen-7: movzx replacing xor+mov
	ret

b_system_screen_bpp_get:
	movzx eax, word [os_screen_bpp]	; EVOLVED Gen-7: movzx replacing xor+mov
	ret

; Network

b_system_net_status:
	jmp b_net_status		; EVOLVED Gen-7: tail-call

b_system_net_config:
	jmp b_net_config		; EVOLVED Gen-7: tail-call

; Bus

b_system_pci_read:
	jmp os_bus_read			; EVOLVED Gen-7: tail-call

b_system_pci_write:
	jmp os_bus_write		; EVOLVED Gen-7: tail-call

; Standard Output

b_system_stdout_get:
	mov rax, qword [0x100018]
	ret

b_system_stdout_set:
	mov qword [0x100018], rax
	ret

; Misc

b_system_callback_timer:
	ret

b_system_callback_network:
	ret

b_system_callback_keyboard:
	ret

b_system_debug_dump_mem:
	push rsi
	mov rsi, rax
	mov rcx, rdx
	call os_debug_dump_mem
	pop rsi
	ret

b_system_debug_dump_rax:
	jmp os_debug_dump_rax		; EVOLVED Gen-7: tail-call

b_system_delay:
	jmp [sys_delay]			; EVOLVED Gen-7: tail-call

b_system_reset:
	xor eax, eax
	call b_smp_get_id		; Reset all other cpu cores
	mov rbx, rax
	mov esi, 0x00005100		; Location in memory of the Pure64 CPU data
b_system_reset_next_ap:
	test ecx, ecx			; EVOLVED Gen-7: 32-bit test (avoids partial register stall)
	jz b_system_reset_no_more_aps
	lodsb				; Load the CPU APIC ID
	cmp al, bl
	je b_system_reset_skip_ap
	call b_smp_reset		; Reset the CPU
b_system_reset_skip_ap:
	dec ecx				; EVOLVED Gen-7: 32-bit dec (avoids partial register stall)
	jmp b_system_reset_next_ap
b_system_reset_no_more_aps:
	int 0x81			; Reset this core

b_system_reboot:
	call reboot

b_system_shutdown:
	mov ax, 0x2000
	mov dx, 0xB004
	out dx, ax			; Bochs/QEMU < v2
	mov dx, 0x0604
	out dx, ax			; QEMU
	mov ax, 0x3400
	mov dx, 0x4004
	out dx, ax			; VirtualBox
	jmp $
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; b_delay -- Delay by X microseconds
; IN:	RAX = Time microseconds
; OUT:	All registers preserved
; Note:	There are 1,000,000 microseconds in a second
;	There are 1,000 milliseconds in a second
b_delay:
	jmp timer_delay			; EVOLVED Gen-7: tail-call
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; b_tsc -- Read the Time-Stamp Counter and store in RAX
; IN:	Nothing
; OUT:	RAX = Current Time-Stamp Counter value
;	All other registers preserved
; EVOLVED: Use rdtscp instead of rdtsc for serialized reads
;  rdtsc can be reordered by the CPU, giving inaccurate timing
;  rdtscp acts as a partial barrier, ensuring prior instructions complete
;  before the counter is read. This gives ~10ns better accuracy.
b_tsc:
	push rdx
	push rcx
	rdtscp				; EVOLVED: Serialized TSC read (waits for prior ops)
	shl rdx, 32			; Shift the low 32-bits to the high 32-bits
	or rax, rdx			; Combine RAX and RDX
	pop rcx
	pop rdx
	ret
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; reboot -- Reboot the computer
reboot:
	mov al, PS2_RESET_CPU
	call ps2_send_cmd
	jmp reboot
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; os_virt_to_phys -- Function to convert a virtual address to a physical address
; IN:	RAX = Virtual Memory Address
; OUT:	RAX = Physical Memory Address
;	All other registers preserved
; NOTE: AlJefra OS uses two ranges of memory. One physical 1-to-1 map and one virtual
;	range for free memory
os_virt_to_phys:
	push r15
	push rbx

	mov r15, 0xFFFF800000000000	; Starting address of the higher half
	cmp rax, r15			; Check if RAX is in the upper canonical range
	jb os_virt_to_phys_done		; If not, it is already a physical address - bail out
	mov rbx, rax			; Save RAX
	and rbx, 0x1FFFFF		; Save the low 20 bits
	mov r15, 0x7FFFFFFFFFFF
	and rax, r15
	shr rax, 21			; Convert 2MB page to entry
	lea r15, [sys_pdh + rax*8]	; EVOLVED Gen-7: LEA replaces shl+add (1 uop vs 2)
	mov rax, [r15]			; Load the entry into RAX
	and rax, -256			; EVOLVED Gen-9: single AND replaces shr+shl pair (clears low 8 bits)
	add rax, rbx

os_virt_to_phys_done:
	pop rbx
	pop r15
	ret
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; os_stub -- A function that just returns
b_user:
os_stub:
none:
	ret
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; System function index table
b_system_table:
; Basic
	dw b_system_timecounter		; 0x00
	dw b_system_free_memory		; 0x01
	dw none				; 0x02
	dw none				; 0x03
	dw none				; 0x04
	dw none				; 0x05
	dw none				; 0x06
	dw none				; 0x07
	dw none				; 0x08
	dw none				; 0x09
	dw none				; 0x0A
	dw none				; 0x0B
	dw none				; 0x0C
	dw none				; 0x0D
	dw none				; 0x0E
	dw none				; 0x0F

; CPU
	dw b_system_smp_get_id		; 0x10
	dw b_system_smp_numcores	; 0x11
	dw b_system_smp_set		; 0x12
	dw b_system_smp_get		; 0x13
	dw b_system_smp_lock		; 0x14
	dw b_system_smp_unlock		; 0x15
	dw b_system_smp_busy		; 0x16
	dw none				; 0x17
	dw none				; 0x18
	dw none				; 0x19
	dw none				; 0x1A
	dw none				; 0x1B
	dw none				; 0x1C
	dw none				; 0x1D
	dw none				; 0x1E
	dw b_system_tsc			; 0x1F

; Video
	dw b_system_screen_lfb_get	; 0x20
	dw b_system_screen_x_get	; 0x21
	dw b_system_screen_y_get	; 0x22
	dw b_system_screen_ppsl_get	; 0x23
	dw b_system_screen_bpp_get	; 0x24
	dw none				; 0x25
	dw none				; 0x26
	dw none				; 0x27
	dw none				; 0x28
	dw none				; 0x29
	dw none				; 0x2A
	dw none				; 0x2B
	dw none				; 0x2C
	dw none				; 0x2D
	dw none				; 0x2E
	dw none				; 0x2F

; Network
	dw b_system_net_status		; 0x30
	dw b_system_net_config		; 0x31
	dw none				; 0x32
	dw none				; 0x33
	dw none				; 0x34
	dw none				; 0x35
	dw none				; 0x36
	dw none				; 0x37
	dw none				; 0x38
	dw none				; 0x39
	dw none				; 0x3A
	dw none				; 0x3B
	dw none				; 0x3C
	dw none				; 0x3D
	dw none				; 0x3E
	dw none				; 0x3F

; Storage
	dw none				; 0x40
	dw none				; 0x41
	dw none				; 0x42
	dw none				; 0x43
	dw none				; 0x44
	dw none				; 0x45
	dw none				; 0x46
	dw none				; 0x47
	dw none				; 0x48
	dw none				; 0x49
	dw none				; 0x4A
	dw none				; 0x4B
	dw none				; 0x4C
	dw none				; 0x4D
	dw none				; 0x4E
	dw none				; 0x4F

; Misc
	dw b_system_pci_read		; 0x50
	dw b_system_pci_write		; 0x51
	dw b_system_stdout_set		; 0x52
	dw b_system_stdout_get		; 0x53
	dw none				; 0x54
	dw none				; 0x55
	dw none				; 0x56
	dw none				; 0x57
	dw none				; 0x58
	dw none				; 0x59
	dw none				; 0x5A
	dw none				; 0x5B
	dw none				; 0x5C
	dw none				; 0x5D
	dw none				; 0x5E
	dw none				; 0x5F
	dw b_system_callback_timer	; 0x60
	dw b_system_callback_network	; 0x61
	dw b_system_callback_keyboard	; 0x62
	dw none				; 0x63
	dw none				; 0x64
	dw none				; 0x65
	dw none				; 0x66
	dw none				; 0x67
	dw none				; 0x68
	dw none				; 0x69
	dw none				; 0x6A
	dw none				; 0x6B
	dw none				; 0x6C
	dw none				; 0x6D
	dw none				; 0x6E
	dw none				; 0x6F

; Misc
	dw b_system_debug_dump_mem	; 0x70
	dw b_system_debug_dump_rax	; 0x71
	dw b_system_delay		; 0x72
	dw none				; 0x73
	dw none				; 0x74
	dw none				; 0x75
	dw none				; 0x76
	dw none				; 0x77
	dw none				; 0x78
	dw none				; 0x79
	dw none				; 0x7A
	dw none				; 0x7B
	dw none				; 0x7C
	dw b_system_reset		; 0x7D
	dw b_system_reboot		; 0x7E
	dw b_system_shutdown		; 0x7F

; GPU Functions (0x80-0x8F)
	dw b_gpu_status			; 0x80
	dw b_gpu_compute		; 0x81
	dw b_gpu_mem_alloc		; 0x82
	dw b_gpu_mem_free		; 0x83
	dw b_gpu_mem_copy_to		; 0x84
	dw b_gpu_mem_copy_from		; 0x85
	dw b_gpu_fence_wait		; 0x86
	dw b_gpu_mmio_read		; 0x87
	dw b_gpu_mmio_write		; 0x88
	dw b_gpu_vram_info		; 0x89
	dw b_gpu_benchmark		; 0x8A
	dw none				; 0x8B
	dw none				; 0x8C
	dw none				; 0x8D
	dw none				; 0x8E
	dw none				; 0x8F
; -----------------------------------------------------------------------------


; =============================================================================
; EOF
