; =============================================================================
; AlJefra OS -- a 64-bit OS written in Assembly for x86-64 systems
; Copyright (C) 2008-2026 AlJefra -- see LICENSE.TXT
;
; Bus Functions
; =============================================================================


; ┌───────────────────────────────────────────────────────────────┐
; │                          RDX Format                           │
; ├───────────────────────────────────────────────────────────────┤
; │63           48 47           32 31   24 23   16 15            0│
; ├───────────────┬───────────────┬───────┬───────┬───────────────┤
; │               │ PCIe Segment  │  Bus  │Device │   Register    │
; └───────────────┴───────────────┴───────┴───────┴───────────────┘
;
; PCIe Segment Group, 16 bits (Ignored for PCI)
; Bus, 8 bits
; Device/Function, 8 bits (5 bit device, 3 bit function)
; Register, 16 bits (Uses 10 bits for PCIe and 6 bits for PCI)


; -----------------------------------------------------------------------------
; os_bus_read -- Read from a register on a bus device
;  IN:	RDX = Register to read from
; OUT:	EAX = Register value that was read
;	All other registers preserved
os_bus_read:
	cmp byte [os_BusEnabled], 2	; Check if PCIe was enabled
	jne os_bus_read_pci		; If not, fall back to PCI
os_bus_read_pcie:
	jmp os_pcie_read		; EVOLVED Gen-6: tail-call, saves call+ret overhead
os_bus_read_pci:
	jmp os_pci_read			; EVOLVED Gen-6: tail-call
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; os_bus_write -- Write to a register on a bus device
;  IN:	RDX = Register to write to
;	EAX = Register value to be written
; OUT:	Nothing, all registers preserved
os_bus_write:
	cmp byte [os_BusEnabled], 2	; Check if PCIe was enabled
	jne os_bus_write_pci		; If not, fall back to PCI
os_bus_write_pcie:
	jmp os_pcie_write		; EVOLVED Gen-6: tail-call
os_bus_write_pci:
	jmp os_pci_write		; EVOLVED Gen-6: tail-call
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; os_bus_read_bar -- Read a BAR (Base Address Register)
;  IN:	RDX = Device to read from (low 8 bits is ignored and set by value in AL)
;	AL = BAR #
; OUT:	RAX = BAR I/O Address
;	RCX = BAR Length in bytes
os_bus_read_bar:
	push rdx
	push rbx

	; Disable Memory and I/O Space
	push rax
	mov dl, 0x01			; Read Status/Command
	call os_bus_read
	btc eax, 1			; Disable Memory Space
	btc eax, 0			; Disable I/O Space
	call os_bus_write		; Write updated Status/Command
	pop rax

	; Read the lower 32-bit BAR and BAR Length
	add al, 0x04			; BARs start at register 4
	mov dl, al
	call os_bus_read		; Returns BAR value in EAX
	mov ebx, eax			; EVOLVED Gen-6: explicit mov replacing 3-uop xchg
	mov eax, 0xFFFFFFFF		; All bits set for BAR sizing
	call os_bus_write		; Write 0xFFFFFFFF to the BAR
	call os_bus_read		; Read the low 32 bits of the length
	push rax
	mov eax, ebx			; Copy original BAR value to EAX
	call os_bus_write		; Write the original value back
	pop rax
	not eax
	mov ecx, eax
	bt ebx, 0			; Bit 0 will be 0 if it is an MMIO space
	jc os_bus_read_bar_io
	bt ebx, 2			; Bit 2 will be 1 if it is a 64-bit MMIO space
	jnc os_bus_read_bar_32bit

	; Read the upper 32-bit BAR and BAR Length
	push r8
	inc dl				; Read next register for next BAR (Upper 32-bits of BAR)
	call os_bus_read
	push rax
	shl rax, 32			; Shift the bits to the upper 32
	and ebx, 0xFFFFFFF0		; Clear the low four bits
	add rax, rbx			; Add the upper 32 and lower 32 together
	mov r8, rax
	or eax, -1			; EVOLVED Gen-9: 1 instruction vs 2 (or imm8 sign-extends to 0xFFFFFFFF, 3 bytes)
	call os_bus_write		; Write 0xFFFFFFFF to the BAR
	call os_bus_read		; Read the low 32 bits of the length
	not eax
	shl rax, 32			; Shift the bits to the upper 32
	add rcx, rax			; Add the upper 32 and lower 32 together
	inc rcx
	pop rax
	call os_bus_write		; Write the original value back
	mov rax, r8
	pop r8

	pop rbx
	pop rdx
	ret

os_bus_read_bar_32bit:
	and ebx, 0xFFFFFFF0		; Clear the low four bits
	mov eax, ebx			; 32-bit BAR address (upper 32 already zero)
	inc rcx

	pop rbx
	pop rdx
	ret

os_bus_read_bar_io:
	and ebx, 0xFFFFFFFC		; Clear the low two bits
	mov eax, ebx			; 32-bit I/O address (upper 32 already zero)

	pop rbx
	pop rdx
	ret
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; Check for a device capability
;  IN:	RDX = Packed Bus address (as per syscalls/bus.asm)
;	CL  = Capability
; OUT:	RDX = Packed Bus address
;	Carry clear = Capability found
;	Carry set = Capability not found
os_bus_cap_check:
	push rax

	mov dl, 1
	call os_bus_read		; Read register 1 for Status/Command
	bt eax, 20			; Check bit 4 of the Status word (31:16)
	jnc os_bus_cap_check_error	; If if doesn't exist then bail out
	mov dl, 13
	call os_bus_read		; Read register 13 for the Capabilities Pointer (7:0)
	and al, 0xFC			; Clear the bottom two bits as they are reserved
os_bus_cap_check_next:
	shr al, 2			; Quick divide by 4
	mov dl, al
	call os_bus_read
	cmp al, cl
	je os_bus_cap_check_done
os_bus_cap_check_next_offset:
	shr eax, 8			; Shift pointer to AL
	test al, al			; EVOLVED Gen-6: test replacing cmp-0
	jne os_bus_cap_check_next	; If not, continue reading, otherwise fall through to error

os_bus_cap_check_error:
	pop rax
	stc				; Set the carry flag
	ret

os_bus_cap_check_done:
	pop rax
	clc				; Clear the carry flag
	ret
; -----------------------------------------------------------------------------


; =============================================================================
; EOF
