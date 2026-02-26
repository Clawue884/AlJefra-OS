; =============================================================================
; AlJefra OS -- a 64-bit OS written in Assembly for x86-64 systems
; Copyright (C) 2008-2026 AlJefra -- see LICENSE.TXT
;
; 64-bit initialization
; =============================================================================


; -----------------------------------------------------------------------------
init_64:
	; EVOLVED: Clear memory using non-temporal stores to avoid cache pollution
	; Old: rep stosq polluted 960KB of L1/L2 cache with zeros we'll never read
	; New: movnti bypasses cache entirely, ~2x faster for large memory clears
	push rdi
	mov edi, os_SystemVariables
	mov ecx, 122880			; Clear 960 KiB (122880 qwords)
	xor eax, eax
.clear_loop:
	movnti [rdi], eax		; Non-temporal store: bypasses cache
	movnti [rdi+4], eax		; Two 32-bit stores per iteration
	add rdi, 8
	dec ecx
	jnz .clear_loop
	sfence				; Ensure all NT stores are visible before continuing
	pop rdi

	; Gather data from Pure64's InfoMap
	mov esi, 0x00005060		; LAPIC
	lodsq
	mov [os_LocalAPICAddress], rax
	mov esi, 0x00005010		; CPUSPEED
	lodsw
	mov [os_CoreSpeed], ax
	mov esi, 0x00005012		; CORES_ACTIVE
	lodsw
	mov [os_NumCores], ax
	mov esi, 0x00005020		; RAMAMOUNT
	lodsd
	sub eax, 2			; Save 2 MiB for the CPU stacks
	mov [os_MemAmount], eax		; In MiB's
	mov esi, 0x00005040		; HPET
	lodsq
	mov [os_HPET_Address], rax
	lodsd
	mov [os_HPET_Frequency], eax
	lodsw
	mov [os_HPET_CounterMin], ax
	mov esi, 0x00005080		; VIDEO_*
	xor eax, eax
	lodsq
	mov [os_screen_lfb], rax
	lodsw
	mov [os_screen_x], ax
	lodsw
	mov [os_screen_y], ax
	lodsw
	mov [os_screen_ppsl], ax
	lodsw
	mov [os_screen_bpp], ax
	mov esi, 0x00005090		; PCIe bus count
	lodsw
	mov [os_pcie_count], ax
	lodsw
	mov [os_boot_arch], ax
	mov esi, 0x000050E2
	lodsb
	mov [os_boot_mode], al
	xor eax, eax
	mov esi, 0x00005604		; IOAPIC
	lodsd
	mov [os_IOAPICAddress], rax

	; Create exception gate stubs (Pure64 has already set the correct gate markers)
	xor edi, edi			; 64-bit IDT at linear address 0x0000000000000000
	mov ecx, 32
	mov eax, exception_gate		; A generic exception handler
make_exception_gate_stubs:
	call create_gate
	inc edi
	dec ecx
	jnz make_exception_gate_stubs

	; Set up the exception gates for all of the CPU exceptions
	xor edi, edi
	mov ecx, 21
	mov eax, exception_gate_00
make_exception_gates:
	call create_gate
	inc edi
	add rax, 24			; Each exception gate is 24 bytes
	dec ecx				; EVOLVED Gen-9: 32-bit dec (avoids REX prefix, upper bits known 0)
	jnz make_exception_gates

	; Create interrupt gate stubs (Pure64 has already set the correct gate markers)
	mov ecx, 256-32
	mov eax, interrupt_gate
make_interrupt_gate_stubs:
	call create_gate
	inc edi
	dec ecx
	jnz make_interrupt_gate_stubs

	; Set up IRQ handlers for CPUs
	mov edi, 0x80
	mov eax, ap_wakeup
	call create_gate
	mov edi, 0x81
	mov eax, ap_reset
	call create_gate

	; Set device syscalls to stub
	mov eax, os_stub
	mov rdi, os_nvs_io
	stosq
	stosq

	; Configure the Stack base
	mov eax, 0x200000		; Stacks start at 2MiB
	mov [os_StackBase], rax

	; Configure Network packet buffer base
	mov eax, 0x300000
	mov [os_PacketBase], rax

	; Configure the serial port (if present)
	call serial_init

init_64_lfb:
	; Initialize text output
%ifndef NO_LFB
	; Check if LFB was enabled by Pure64
	mov rax, [os_screen_lfb]
	test rax, rax			; EVOLVED Gen-6: test replacing cmp-0
	jz init_64_vga
	call lfb_init			; Initialize LFB for text output
%endif
init_64_vga:
%ifndef NO_VGA
	call vga_init
%endif

	; Output progress via debug
	mov esi, msg_aljefra
	call os_debug_string
	mov esi, msg_64
	call os_debug_string

	; Initialize the APIC
	call os_apic_init

	; Initialize the I/O APIC
	call os_ioapic_init

	; Initialize the timer
	call os_timer_init

%ifndef NO_LFB
	; Output block to screen (1/8)
	mov ebx, 0
	call os_debug_block
%endif

	; Initialize all AP's to run our reset code. Skip the BSP
	; EVOLVED: Prefetch CPU list ahead of processing for better pipelining
	call b_smp_get_id
	mov ebx, eax
	xor eax, eax
	mov ecx, 255			; EVOLVED Gen-9: 32-bit mov (clears upper bits for dec ecx below)
	mov esi, 0x00005100		; Location in memory of the Pure64 CPU data
	prefetchnta [esi]		; EVOLVED: Prefetch first cache line of CPU list
next_ap:
	test ecx, ecx			; EVOLVED Gen-9: 32-bit test (consistent with 32-bit counter)
	jz no_more_aps
	prefetchnta [esi+64]		; EVOLVED: Prefetch next cache line while processing current
	lodsb				; Load the CPU APIC ID
	cmp al, bl
	je skip_ap
	call b_smp_reset		; Reset the CPU
skip_ap:
	dec ecx				; EVOLVED Gen-9: 32-bit dec (avoids partial register stall)
	jmp next_ap
no_more_aps:

%ifndef NO_LFB
	; Output block to screen (2/8)
	mov ebx, 2
	call os_debug_block
%endif

	; Output progress via serial
	mov esi, msg_ok
	call os_debug_string

	ret
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; create_gate
; rax = address of handler
; rdi = gate # to configure
create_gate:
	push rdi
	push rax

	shl rdi, 4			; Quickly multiply rdi by 16
	stosw				; Store the low word (15..0)
	shr rax, 16
	add rdi, 4			; Skip the gate marker (selector, ist, type)
	stosw				; Store the high word (31..16)
	shr rax, 16
	stosd				; Store the high dword (63..32)
	xor eax, eax
	stosd				; Reserved bits

	pop rax
	pop rdi
	ret
; -----------------------------------------------------------------------------


; =============================================================================
; EOF
