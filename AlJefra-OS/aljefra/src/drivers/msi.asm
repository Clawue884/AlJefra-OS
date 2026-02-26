; =============================================================================
; AlJefra OS -- a 64-bit OS written in Assembly for x86-64 systems
; Copyright (C) 2008-2026 AlJefra -- see LICENSE.TXT
;
; Message Signaled Interrupts (MSI-X and MSI)
; =============================================================================


; -----------------------------------------------------------------------------
; Initialize MSI-X for a device
;  IN:	RDX = Packed Bus address (as per syscalls/bus.asm)
;	AL  = Start Vector
; OUT:	Carry flag (clear on success, set on error)
; -----------------------------------------------------------------------------
; Message Control - Enable (15), Function Mask (14), Table Size (10:0)
;
; Example MSI-X Entry (From QEMU xHCI Controller)
; 000FA011 <- Cap ID 0x11 (MSI-X), next ptr 0xA0, message control 0x000F - Table size is bits 10:0 so 0x0F
; 00003000 <- BIR (2:0) is 0x0 so BAR0, Table Offset (31:3) - 8-byte aligned so clear low 3 bits - 0x3000 in this case
; 00003800 <- Pending Bit BIR (2:0) and Pending Bit Offset (31:3) - 0x3800 in this case
;
; Example MSI-X Entry (From QEMU Virtio-Net)
; 00038411 <- Cap ID 0x11 (MSI-X), next ptr 0x84, message control 0x0003 - Table size is bits 10:0 so 3 (n-1 so table size is actually 4)
; 00000001 <- BIR (2:0) is 0x1 so BAR1, Table Offset (31:3) - 8-byte aligned so clear low 3 bits - 0x0 in this case
; 00000801 <- Pending Bit BIR (2:0) is 0x1 so BAR1 and Pending Bit Offset (31:3) is 0x800
;
; Resulting MSI-X table entry in memory should look similar to:
; 0xXXXXXXXX: FEE00000 00000000 000040XX 00000000
msix_init:
	push r8
	push rdi
	push rdx
	push rcx
	push rbx
	push rax

	mov r8b, al

	; Check for MSI-X in PCI Capabilities
	mov cl, 0x11			; PCI Capability ID for MSI-X
	call os_bus_cap_check
	jc msix_init_error		; os_bus_cap_check sets carry flag is the cap isn't found

	push rdx			; Save packed bus address

	; Enable MSI-X, Mask it, Get Table Size
msix_init_enable:
	call os_bus_read
	mov ecx, eax			; Save for Table Size
	bts eax, 31			; Enable MSI-X
	bts eax, 30			; Set Function Mask
	call os_bus_write
	shr ecx, 16			; Shift Message Control to low 16-bits
	and cx, 0x7FF			; Keep bits 10:0
	; Read the BIR and Table Offset
	push rdx
	add dl, 1
	call os_bus_read
	mov ebx, eax			; EBX for the Table Offset
	and ebx, 0xFFFFFFF8		; Clear bits 2:0
	and eax, 0x00000007		; Keep bits 2:0 for the BIR

	add al, 0x04			; Add offset to start of BARs
	mov dl, al
	call os_bus_read		; Read the BAR address

; TODO - Read BAR properly
;	push rcx			; Save RCX as os_bus_read_bar returns a value in it
;	call os_bus_read_bar		; Read the BAR address
;	pop rcx

	add rax, rbx			; Add offset to base
	and eax, 0xFFFFFFF8		; Clear bits 2:0 of a 32-bit BAR
	mov rdi, rax
	pop rdx

	; Configure MSI-X Table
	; EVOLVED: Optimized MSI-X entry writing with fewer redundant loads
	inc cx				; EVOLVED: inc is more efficient than add cx, 1
	xor ebx, ebx			; Trigger Mode (15), Level (14), Delivery Mode (10:8), Vector (7:0)
	mov bl, r8b			; Store start vector
	mov r8, [os_LocalAPICAddress]	; EVOLVED: Load APIC address once outside loop
msix_init_create_entry:
	mov rax, r8			; EVOLVED: Use cached APIC address (was re-reading from memory each iteration)
	stosd				; Store Message Address Low
	shr rax, 32			; Shift the high bits to EAX
	stosd				; Store Message Address High
	mov eax, ebx
	inc ebx
	stosd				; Store Message Data
	xor eax, eax			; Bits 31:1 are reserved, Masked (0) - 1 for masked
	stosd				; Store Vector Control
	dec ecx				; EVOLVED Gen-7: 32-bit dec (avoids partial register stall, Exp-B)
	jnz msix_init_create_entry

	; Unmask MSI-X via bus
	pop rdx				; Restore packed bus address
	call os_bus_read
	btr eax, 30			; Clear Function Mask
	call os_bus_write

	pop rax
	pop rbx
	pop rcx
	pop rdx
	pop rdi
	pop r8
	clc				; Clear the carry flag
	ret

msix_init_error:
	pop rax
	pop rbx
	pop rcx
	pop rdx
	pop rdi
	pop r8
	stc				; Set the carry flag
	ret
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; Initialize MSI for a device
;  IN:	RDX = Packed Bus address (as per syscalls/bus.asm)
;	AL  = Start Vector
; OUT:	Carry flag (clear on success, set on error)
; -----------------------------------------------------------------------------
; Example MSI Entry (From Intel test system)
; 00869005 <- Cap ID 0x05 (MSI), next ptr 0x90, message control 0x0x0086 (64-bit, MMC 8)
; 00000000 <- Message Address Low
; 00000000 <- Message Address High
; 00000000 <- Message Data (15:0)
; 00000000 <- Mask (only exists if Per-vector masking is enabled)
; 00000000 <- Pending (only exists if Per-vector masking is enabled)
; Message Control - Per-vector masking (8), 64-bit (7), Multiple Message Enable (6:4), Multiple Message Capable (3:1), Enable (0)
; MME/MMC 000b = 1, 001b = 2, 010b = 4, 011b = 8, 100b = 16, 101b = 32
; Todo - Test bit 7, Check Multiple Message Capable, copy to Multiple Message Enable
msi_init:
	push rdx
	push rcx
	push rbx
	push rax

	mov bl, al

	; Check for MSI in PCI Capabilities
	mov cl, 0x05			; PCI Capability ID for MSI
	call os_bus_cap_check
	jc msi_init_error

	; Enable MSI
msi_init_enable:
	push rdx
	add dl, 1
	mov rax, [os_LocalAPICAddress]	; 0xFEE for bits 31:20, Dest (19:12), RH (3), DM (2)
	call os_bus_write		; Store Message Address Low
	add dl, 1
	shr rax, 32			; Rotate the high bits to EAX
	call os_bus_write		; Store Message Address High
	add dl, 1
	mov eax, 0x00004000		; Trigger Mode (15), Level (14), Delivery Mode (10:8), Vector (7:0)
	mov al, bl			; Store start vector
	call os_bus_write		; Store Message Data
	sub dl, 3
	call os_bus_read		; Get Message Control
	bts eax, 21			; Debug - See MME to 8
	bts eax, 20			; Debug - See MME to 8
	bts eax, 16			; Set Enable
	call os_bus_write		; Update Message Control

	; Unmask MSI via bus
	pop rdx
	call os_bus_read
	btr eax, 30			; Clear Function Mask
	call os_bus_write

msi_init_done:
	pop rax
	pop rbx
	pop rcx
	pop rdx
	clc				; Clear the carry flag
	ret

msi_init_error:
	pop rax
	pop rbx
	pop rcx
	pop rdx
	stc				; Set the carry flag
	ret
; -----------------------------------------------------------------------------


; =============================================================================
; EOF