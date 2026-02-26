; =============================================================================
; AlJefra OS -- a 64-bit OS written in Assembly for x86-64 systems
; Copyright (C) 2008-2026 AlJefra -- see LICENSE.TXT
;
; Interrupts
; =============================================================================


; -----------------------------------------------------------------------------
; Default exception handler
align 8
exception_gate:
	mov esi, int_string00
	call b_output
	mov esi, exc_string
	call b_output
	jmp $				; Hang
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; Default interrupt handler
align 8
interrupt_gate:				; handler for all other interrupts
	iretq				; It was an undefined interrupt so return to caller
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; Keyboard interrupt. IRQ 0x01, INT 0x21
; This IRQ runs whenever there is input on the keyboard
; EVOLVED: Targeted wakeup instead of broadcast storm
align 8
int_keyboard:
	push rcx
	push rax
	push rbx

	call ps2_keyboard_interrupt	; Call keyboard interrupt code in PS/2 driver

	; EVOLVED Gen-8: Inline EOI — eliminates call overhead in hot interrupt path
	mov rax, [os_LocalAPICAddress]
	mov dword [rax + APIC_EOI], 0

	; EVOLVED: Only wake the BSP or a single core waiting for input
	; instead of broadcasting to ALL cores (was causing N-way cache
	; invalidation on every keystroke)
	xor eax, eax
	mov al, [os_BSP]		; Wake only the BSP
	call b_smp_wakeup

	; Check if a keyboard callback is registered
	mov rax, [os_KeyboardCallback]
	test rax, rax
	jz .no_callback
	call rax			; Call the registered handler
.no_callback:

	pop rbx
	pop rax
	pop rcx
	iretq
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; Serial interrupt. IRQ 0x04, INT 0x24
; This IRQ runs whenever there is input on the serial port
; EVOLVED: Targeted wakeup instead of broadcast storm
align 8
int_serial:
	push rax			; EVOLVED Gen-10: removed dead push/pop rcx (callee-save convention)

	call serial_interrupt		; Call interrupt code in serial driver

	; EVOLVED Gen-8: Inline EOI
	mov rax, [os_LocalAPICAddress]
	mov dword [rax + APIC_EOI], 0

	; EVOLVED: Only wake the BSP instead of all cores
	xor eax, eax
	mov al, [os_BSP]
	call b_smp_wakeup

	pop rax
	iretq
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; HPET Timer 0 interrupt
; This IRQ runs whenever HPET Timer 0 expires
; EVOLVED: Now drives preemptive work queue checking and load balancing
align 8
hpet:
	push rcx
	push rax
	push rbx

	; EVOLVED Gen-8: Inline EOI (reduce interrupt latency)
	mov rax, [os_LocalAPICAddress]
	mov dword [rax + APIC_EOI], 0

	; EVOLVED: Increment system tick counter for scheduling
	lock inc qword [os_hpet_ticks]

	; EVOLVED: Check if any AP has pending work in the AI scheduler queue
	; This enables preemptive task rotation every timer tick
	cmp dword [ai_sched_count], 0
	je .no_pending_work

	; Wake idle cores that have pending work
	; Only wake one core per tick to avoid thundering herd
	call hpet_wake_idle_core

.no_pending_work:
	; Call clock callback if registered
	mov rax, [os_ClockCallback]
	test rax, rax
	jz .no_callback
	call rax
.no_callback:

	pop rbx
	pop rax
	pop rcx
	iretq

; EVOLVED: Wake a single idle core for pending work
hpet_wake_idle_core:
	push rsi			; EVOLVED Gen-10: removed push/pop rcx (caller saves it)

	mov rsi, os_SMP
	xor ecx, ecx			; Core counter
	align 16			; EVOLVED Gen-6: align hot scan loop
.scan:					; EVOLVED Gen-10: bottom-test loop (saves 1 jmp)
	mov rax, [rsi]
	test al, 1			; EVOLVED Gen-8: test replacing bt (shorter encoding)
	jnc .next
	and rax, 0xFFFFFFFFFFFFFFF0	; Clear flags
	test rax, rax			; Is core idle (no code address)?
	jnz .next			; Skip busy cores
	; Found idle core - wake it
	mov eax, ecx
	call b_smp_wakeup
	jmp .scan_done
.next:
	add rsi, 8
	inc ecx
	cmp ecx, 256
	jl .scan
.scan_done:
	pop rsi
	ret
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; A simple interrupt that just acknowledges an IPI. Useful for getting an AP past a 'hlt' in the code.
align 8
ap_wakeup:
	push rax
	; EVOLVED Gen-8: Inline EOI — eliminates call/ret + push/pop overhead
	mov rax, [os_LocalAPICAddress]
	mov dword [rax + APIC_EOI], 0
	pop rax
	iretq				; Return from the IPI
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; Resets a CPU to execute ap_clear
align 8
ap_reset:
	; Don't use 'os_apic_write' as we can't guarantee the state of the stack
	mov eax, ap_clear		; Set RAX to the address of ap_clear
	mov [rsp], rax			; Overwrite the return address on the CPU's stack
	mov rdi, [os_LocalAPICAddress]	; Acknowledge the IPI
	add rdi, 0xB0
	xor eax, eax
	stosd
	iretq				; Return from the IPI. CPU will execute code at ap_clear
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; CPU Exception Gates
align 8
exception_gate_00:			; DE (Division Error)
	mov [rsp-16], rax
	xor eax, eax
	mov [rsp-8], rax
	sub rsp, 16
	mov al, 0x00
	jmp exception_gate_main

align 8
exception_gate_01:			; DB
	mov [rsp-16], rax
	xor eax, eax
	mov [rsp-8], rax
	sub rsp, 16
	mov al, 0x01
	jmp exception_gate_main

align 8
exception_gate_02:
	mov [rsp-16], rax
	xor eax, eax
	mov [rsp-8], rax
	sub rsp, 16
	mov al, 0x02
	jmp exception_gate_main

align 8
exception_gate_03:			; BP
	mov [rsp-16], rax
	xor eax, eax
	mov [rsp-8], rax
	sub rsp, 16
	mov al, 0x03
	jmp exception_gate_main

align 8
exception_gate_04:			; OF
	mov [rsp-16], rax
	xor eax, eax
	mov [rsp-8], rax
	sub rsp, 16
	mov al, 0x04
	jmp exception_gate_main

align 8
exception_gate_05:			; BR
	mov [rsp-16], rax
	xor eax, eax
	mov [rsp-8], rax
	sub rsp, 16
	mov al, 0x05
	jmp exception_gate_main

align 8
exception_gate_06:			; UD (Invalid Opcode)
	mov [rsp-16], rax
	xor eax, eax
	mov [rsp-8], rax
	sub rsp, 16
	mov al, 0x06
	jmp exception_gate_main

align 8
exception_gate_07:			; NM
	mov [rsp-16], rax
	xor eax, eax
	mov [rsp-8], rax
	sub rsp, 16
	mov al, 0x07
	jmp exception_gate_main

align 8
exception_gate_08:			; DF
	push rax
	mov al, 0x08
	jmp exception_gate_main
	times 16 db 0x90

align 8
exception_gate_09:
	mov [rsp-16], rax
	xor eax, eax
	mov [rsp-8], rax
	sub rsp, 16
	mov al, 0x09
	jmp exception_gate_main

align 8
exception_gate_10:			; TS
	push rax
	mov al, 0x0A
	jmp exception_gate_main
	times 16 db 0x90

align 8
exception_gate_11:			; NP
	push rax
	mov al, 0x0B
	jmp exception_gate_main
	times 16 db 0x90

align 8
exception_gate_12:			; SS
	push rax
	mov al, 0x0C
	jmp exception_gate_main
	times 16 db 0x90

align 8
exception_gate_13:			; GP
	push rax
	mov al, 0x0D
	jmp exception_gate_main
	times 16 db 0x90

align 8
exception_gate_14:			; PF (Page Fault)
	; An error code is store in RAX (EAX padded)
	; Register CR2 is set to the virtual address which caused the Page Fault
	push rax
	mov al, 0x0E
	jmp exception_gate_main
	times 16 db 0x90

align 8
exception_gate_15:
	mov [rsp-16], rax
	xor eax, eax
	mov [rsp-8], rax
	sub rsp, 16
	mov al, 0x0F
	jmp exception_gate_main

align 8
exception_gate_16:			; MF
	mov [rsp-16], rax
	xor eax, eax
	mov [rsp-8], rax
	sub rsp, 16
	mov al, 0x10
	jmp exception_gate_main

align 8
exception_gate_17:			; AC
	push rax
	mov al, 0x11
	jmp exception_gate_main
	times 16 db 0x90

align 8
exception_gate_18:			; MC
	mov [rsp-16], rax
	xor eax, eax
	mov [rsp-8], rax
	sub rsp, 16
	mov al, 0x12
	jmp exception_gate_main

align 8
exception_gate_19:			; XM
	mov [rsp-16], rax
	xor eax, eax
	mov [rsp-8], rax
	sub rsp, 16
	mov al, 0x13
	jmp exception_gate_main

align 8
exception_gate_20:			; VE
	mov [rsp-16], rax
	xor eax, eax
	mov [rsp-8], rax
	sub rsp, 16
	mov al, 0x14
	jmp exception_gate_main

; -----------------------------------------------------------------------------
; Main exception handler
align 8
exception_gate_main:
	mov qword [os_NetworkCallback], 0	; Reset the network callback
	mov qword [os_ClockCallback], 0		; Reset the clock callback

	; Display exception message, APIC ID, and exception type
	push rbx
	push rdi
	push rsi
	push rcx			; Char counter for b_output
	push rax			; Save RAX since b_smp_get_id clobbers it
	call os_debug_newline
	mov esi, int_string00
	mov ecx, 6
	call b_output
	call b_smp_get_id		; Get the local CPU ID and print it
	call os_debug_dump_ax
	mov esi, int_string01
	mov ecx, 15
	call b_output
	mov esi, exc_string00
	pop rax
	and eax, 0xFF			; EVOLVED Gen-6: simplified mask
	push rax
	; EVOLVED Gen-6: LEA+SHL replacing mul bl (3-cycle → 2-cycle)
	lea eax, [rax + rax*2]		; EAX = AL * 3
	shl eax, 1			; EAX = AL * 6
	add rsi, rax			; Use the value in RAX as an offset to get to the right message
	pop rax
	mov bl, 0x0F
	mov ecx, 6
	call b_output
	pop rcx
	pop rsi
	pop rdi
	pop rbx
	pop rax

	; Dump all registers
	push r15
	push r14
	push r13
	push r12
	push r11
	push r10
	push r9
	push r8
	push rsp
	push rbp
	push rdi
	push rsi
	push rdx
	push rcx
	push rbx
	push rax
	mov esi, reg_string00		; Load address of first register string
	mov ecx, 4			; Number of characters per reg_string
	mov edx, 16			; Counter of registers to left to output
	xor ebx, ebx			; Counter of registers output per line
	call os_debug_newline
exception_gate_main_nextreg:
	call b_output
	add esi, 4
	pop rax
	call os_debug_dump_rax
	inc ebx				; EVOLVED Gen-8: inc replacing add-1
	cmp ebx, 4			; Number of registers to output per line
	jne exception_gate_main_nextreg_space
	call os_debug_newline
	xor ebx, ebx
	jmp exception_gate_main_nextreg_continue
exception_gate_main_nextreg_space:
	call os_debug_space
exception_gate_main_nextreg_continue:
	dec edx
	jnz exception_gate_main_nextreg
	call b_output
	mov rax, [rsp+8] 		; RIP of caller
	call os_debug_dump_rax
	call os_debug_space
	add esi, 4
	call b_output
	mov rax, cr2
	call os_debug_dump_rax

	; Check if the exception was on the BSP. Rerun the payload if so
	call b_smp_get_id		; Get the local CPU ID
	cmp [os_BSP], al
	je start_payload
	jmp ap_clear			; jump to AP clear code
; -----------------------------------------------------------------------------


int_string00 db 'CPU 0x'
int_string01 db ' - Exception 0x'
; Strings for the error messages
exc_string db 'Unknown Fatal Exception!'
exc_string00 db '00(DE)'
exc_string01 db '01(DB)'
exc_string02 db '02    '
exc_string03 db '03(BP)'
exc_string04 db '04(OF)'
exc_string05 db '05(BR)'
exc_string06 db '06(UD)'
exc_string07 db '07(NM)'
exc_string08 db '08(DF)'
exc_string09 db '09    '	; No longer generated on new CPU's
exc_string10 db '10(TS)'
exc_string11 db '11(NP)'
exc_string12 db '12(SS)'
exc_string13 db '13(GP)'
exc_string14 db '14(PF)'
exc_string15 db '15    '
exc_string16 db '16(MF)'
exc_string17 db '17(AC)'
exc_string18 db '18(MC)'
exc_string19 db '19(XM)'
exc_string20 db '20(VE)'

; Strings for registers
reg_string00 db 'RAX='
reg_string01 db 'RBX='
reg_string02 db 'RCX='
reg_string03 db 'RDX='
reg_string04 db 'RSI='
reg_string05 db 'RDI='
reg_string06 db 'RBP='
reg_string07 db 'RSP='
reg_string08 db 'R8 ='
reg_string09 db 'R9 ='
reg_string10 db 'R10='
reg_string11 db 'R11='
reg_string12 db 'R12='
reg_string13 db 'R13='
reg_string14 db 'R14='
reg_string15 db 'R15='
reg_string16 db 'RIP='
reg_string17 db 'CR2='


; =============================================================================
; EOF
