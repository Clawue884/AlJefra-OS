; =============================================================================
; AlJefra OS -- a 64-bit OS written in Assembly for x86-64 systems
; Copyright (C) 2008-2026 AlJefra -- see LICENSE.TXT
;
; Intel e1000/e1000e NIC Driver
;
; Supports: 82540EM (QEMU), 82545EM, 82574L, I217-LM, I219-LM/V
; =============================================================================


; e1000 Register Offsets
E1000_CTRL		equ 0x0000	; Device Control
E1000_STATUS		equ 0x0008	; Device Status
E1000_EERD		equ 0x0014	; EEPROM Read
E1000_ICR		equ 0x00C0	; Interrupt Cause Read
E1000_IMS		equ 0x00D0	; Interrupt Mask Set
E1000_IMC		equ 0x00D8	; Interrupt Mask Clear
E1000_RCTL		equ 0x0100	; Receive Control
E1000_RDBAL		equ 0x2800	; RX Descriptor Base Low
E1000_RDBAH		equ 0x2804	; RX Descriptor Base High
E1000_RDLEN		equ 0x2808	; RX Descriptor Length
E1000_RDH		equ 0x2810	; RX Descriptor Head
E1000_RDT		equ 0x2818	; RX Descriptor Tail
E1000_TCTL		equ 0x0400	; Transmit Control
E1000_TDBAL		equ 0x3800	; TX Descriptor Base Low
E1000_TDBAH		equ 0x3804	; TX Descriptor Base High
E1000_TDLEN		equ 0x3808	; TX Descriptor Length
E1000_TDH		equ 0x3810	; TX Descriptor Head
E1000_TDT		equ 0x3818	; TX Descriptor Tail
E1000_RAL0		equ 0x5400	; Receive Address Low
E1000_RAH0		equ 0x5404	; Receive Address High
E1000_MTA		equ 0x5200	; Multicast Table Array (128 DWORDs)

; CTRL Register bits
E1000_CTRL_SLU		equ (1 << 6)	; Set Link Up
E1000_CTRL_RST		equ (1 << 26)	; Device Reset
E1000_CTRL_ASDE	equ (1 << 5)	; Auto-Speed Detection Enable
E1000_CTRL_PHY_RST	equ (1 << 31)	; PHY Reset

; RCTL Register bits
E1000_RCTL_EN		equ (1 << 1)	; Receiver Enable
E1000_RCTL_SBP		equ (1 << 2)	; Store Bad Packets
E1000_RCTL_UPE		equ (1 << 3)	; Unicast Promiscuous
E1000_RCTL_MPE		equ (1 << 4)	; Multicast Promiscuous
E1000_RCTL_BAM		equ (1 << 15)	; Broadcast Accept Mode
E1000_RCTL_BSIZE_2048	equ (0 << 16)	; Buffer Size 2048 bytes
E1000_RCTL_SECRC	equ (1 << 26)	; Strip Ethernet CRC

; TCTL Register bits
E1000_TCTL_EN		equ (1 << 1)	; Transmitter Enable
E1000_TCTL_PSP		equ (1 << 3)	; Pad Short Packets
E1000_TCTL_CT_SHIFT	equ 4		; Collision Threshold shift
E1000_TCTL_COLD_SHIFT	equ 12		; Collision Distance shift

; TX Descriptor Command bits
E1000_TXD_CMD_EOP	equ 0x01	; End of Packet
E1000_TXD_CMD_IFCS	equ 0x02	; Insert FCS
E1000_TXD_CMD_RS	equ 0x08	; Report Status
; TX Descriptor Status bits
E1000_TXD_STAT_DD	equ 0x01	; Descriptor Done

; RX Descriptor Status bits
E1000_RXD_STAT_DD	equ 0x01	; Descriptor Done
E1000_RXD_STAT_EOP	equ 0x02	; End of Packet

E1000_NUM_RX_DESC	equ 256		; Number of RX descriptors
E1000_NUM_TX_DESC	equ 256		; Number of TX descriptors
E1000_RX_BUF_SIZE	equ 2048	; RX buffer size


; -----------------------------------------------------------------------------
; Initialize an Intel e1000/e1000e NIC
;  IN:	RDX = Packed Bus address (as per syscalls/bus.asm)
;	R8D = Device ID (high 16) / Vendor ID (low 16)
net_e1000_init:
	push rdi
	push rsi
	push rdx
	push rcx
	push rbx
	push rax

	; Get entry in net_table for this interface
	mov rdi, net_table
	xor eax, eax
	mov al, [os_net_icount]
	shl eax, 7			; Multiply by 128
	add rdi, rax

	; Store driver ID (0x8086 for Intel)
	mov ax, 0x8086
	stosw
	push rdi			; Save offset into net_table

	add rdi, 14			; Skip to nt_base (offset 0x10)

	; Read BAR0 for MMIO base address
	mov al, 0			; BAR0
	call os_bus_read_bar
	stosq				; Store base address at nt_base
	push rax			; Save MMIO base
	mov rax, rcx
	stosq				; Store BAR length

	; Enable Bus Master + Memory Space in PCI Command register
	mov dl, 0x01			; Status/Command register
	call os_bus_read
	bts eax, 2			; Enable Bus Master
	bts eax, 1			; Enable Memory Space
	bts eax, 10			; Disable legacy interrupts
	call os_bus_write

	; Reset the device
	pop rsi				; RSI = MMIO base address
	push rsi

	; Software reset: set CTRL.RST
	mov eax, [rsi + E1000_CTRL]
	or eax, E1000_CTRL_RST
	mov [rsi + E1000_CTRL], eax

	; Wait for reset to complete (~1ms, poll CTRL.RST to clear)
	mov ecx, 100000
net_e1000_init_reset_wait:
	pause
	mov eax, [rsi + E1000_CTRL]
	test eax, E1000_CTRL_RST
	jz net_e1000_init_reset_done
	dec ecx
	jnz net_e1000_init_reset_wait
net_e1000_init_reset_done:

	; Disable all interrupts
	mov dword [rsi + E1000_IMC], 0xFFFFFFFF
	; Clear pending interrupts
	mov eax, [rsi + E1000_ICR]

	; Set Link Up and Auto-Speed Detection
	mov eax, [rsi + E1000_CTRL]
	or eax, E1000_CTRL_SLU | E1000_CTRL_ASDE
	and eax, ~E1000_CTRL_PHY_RST	; Clear PHY reset
	mov [rsi + E1000_CTRL], eax

	; Read MAC address from RAL0/RAH0 (already programmed by firmware/EEPROM)
	pop rax				; MMIO base
	pop rdi				; net_table offset (after nt_ID)
	push rdi
	push rax

	; Try reading MAC from EEPROM first via EERD
	mov rsi, rax			; RSI = MMIO base
	call net_e1000_read_mac_eeprom
	jnc net_e1000_init_mac_from_eeprom

	; Fallback: read from RAL0/RAH0
	mov eax, [rsi + E1000_RAL0]
	mov [rdi + 6], eax		; Lower 4 bytes of MAC at nt_MAC
	mov eax, [rsi + E1000_RAH0]
	mov [rdi + 10], ax		; Upper 2 bytes of MAC
	jmp net_e1000_init_mac_done

net_e1000_init_mac_from_eeprom:
	; MAC already stored by net_e1000_read_mac_eeprom into [rdi+6]
	; Also program it into RAL0/RAH0
	mov eax, [rdi + 6]
	mov [rsi + E1000_RAL0], eax
	xor eax, eax
	mov ax, [rdi + 10]
	or eax, (1 << 31)		; RAH.AV (Address Valid)
	mov [rsi + E1000_RAH0], eax

net_e1000_init_mac_done:

	; Clear Multicast Table Array (128 DWORDs = 512 bytes)
	lea rdi, [rsi + E1000_MTA]
	xor eax, eax
	mov ecx, 128
net_e1000_init_clear_mta:
	mov [rdi], eax
	add rdi, 4
	dec ecx
	jnz net_e1000_init_clear_mta

	; Set up RX and TX descriptor ring base addresses in net_table
	pop rax				; MMIO base
	pop rdi				; net_table offset
	push rdi
	push rax

	; Calculate descriptor addresses from os_rx_desc/os_tx_desc
	xor ecx, ecx
	mov cl, byte [os_net_icount]
	shl ecx, 15			; Offset per NIC (32KB each)
	add rdi, 0x2E			; Skip to nt_tx_desc (offset 0x30)
	mov rax, os_tx_desc
	add rax, rcx
	stosq				; Store TX descriptor base
	mov rax, os_rx_desc
	add rax, rcx
	stosq				; Store RX descriptor base

	; Set up RX Descriptor Ring
	pop rsi				; RSI = MMIO base
	push rsi

	; Get RX descriptor base from net_table
	mov rdi, net_table
	xor eax, eax
	mov al, [os_net_icount]
	shl eax, 7
	add rdi, rax
	mov r8, rdi			; R8 = net_table entry base
	mov rax, [rdi + nt_rx_desc]

	; Program RX descriptor ring into hardware
	mov [rsi + E1000_RDBAL], eax
	shr rax, 32
	mov [rsi + E1000_RDBAH], eax
	mov dword [rsi + E1000_RDLEN], E1000_NUM_RX_DESC * 16	; Each descriptor = 16 bytes
	mov dword [rsi + E1000_RDH], 0	; Head = 0
	mov dword [rsi + E1000_RDT], 0	; Will set after populating

	; Populate RX descriptors with packet buffer addresses
	mov rdi, [r8 + nt_rx_desc]
	mov rbx, [os_PacketBase]
	xor ecx, ecx
net_e1000_init_pop_rx:
	mov rax, rbx			; Buffer address
	stosq				; Descriptor bytes 0-7: Buffer Address
	xor eax, eax
	stosq				; Descriptor bytes 8-15: Status/Length (zero initially)
	add rbx, E1000_RX_BUF_SIZE
	inc ecx
	cmp ecx, E1000_NUM_RX_DESC
	jl net_e1000_init_pop_rx
	mov [os_PacketBase], rbx	; Advance packet base for next NIC

	; Set RX Tail to last descriptor (enables all descriptors)
	mov dword [rsi + E1000_RDT], E1000_NUM_RX_DESC - 1
	mov dword [r8 + nt_rx_head], 0	; Initialize RX head tracking

	; Enable Receiver
	mov eax, E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_BSIZE_2048 | E1000_RCTL_SECRC
	mov [rsi + E1000_RCTL], eax

	; Set up TX Descriptor Ring
	mov rax, [r8 + nt_tx_desc]
	mov [rsi + E1000_TDBAL], eax
	shr rax, 32
	mov [rsi + E1000_TDBAH], eax
	mov dword [rsi + E1000_TDLEN], E1000_NUM_TX_DESC * 16
	mov dword [rsi + E1000_TDH], 0
	mov dword [rsi + E1000_TDT], 0

	; Zero TX descriptors
	; EVOLVED Gen-6: rep stosq replacing manual loop (~4x faster for 4KB)
	mov rdi, [r8 + nt_tx_desc]
	xor eax, eax
	mov ecx, E1000_NUM_TX_DESC * 2	; 2 QWORDs per descriptor
	rep stosq

	; Initialize TX tracking
	mov dword [r8 + nt_tx_tail], 0
	mov dword [r8 + nt_tx_head], 0

	; Enable Transmitter
	; CT = 0x10 (16 retries), COLD = 0x40 (full-duplex)
	mov eax, E1000_TCTL_EN | E1000_TCTL_PSP | (0x10 << E1000_TCTL_CT_SHIFT) | (0x40 << E1000_TCTL_COLD_SHIFT)
	mov [rsi + E1000_TCTL], eax

	; Store function pointers in net_table
	pop rsi				; MMIO base (discard)
	pop rdi				; net_table offset
	add rdi, 0x16			; Skip to nt_config (offset 0x18)
	mov rax, net_e1000_config
	stosq
	mov rax, net_e1000_transmit
	stosq
	mov rax, net_e1000_poll
	stosq

	; Enable interrupts for RX (optional, we use polling)
	; For now, leave interrupts disabled — polling model

	pop rax
	pop rbx
	pop rcx
	pop rdx
	pop rsi
	pop rdi
	ret
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; net_e1000_read_mac_eeprom - Read MAC address from EEPROM via EERD register
;  IN:	RSI = MMIO base address
;	RDI = net_table entry (after nt_ID, so +6 = nt_MAC)
; OUT:	CF clear if success, CF set if EEPROM not available
net_e1000_read_mac_eeprom:
	push rcx			; EVOLVED Gen-6: removed unused push rbx
	push rax

	; Read word 0 (MAC bytes 0-1)
	xor eax, eax
	mov al, 0x00			; EEPROM address 0
	shl eax, 8			; Shift to address field
	or eax, 1			; Set Start bit
	mov [rsi + E1000_EERD], eax

	; Poll for completion (bit 4 = Done)
	mov ecx, 10000
net_e1000_eeprom_wait0:
	pause
	mov eax, [rsi + E1000_EERD]
	test eax, (1 << 4)
	jnz net_e1000_eeprom_word0_done
	dec ecx
	jnz net_e1000_eeprom_wait0
	; Timeout — EEPROM not available
	stc
	jmp net_e1000_eeprom_done

net_e1000_eeprom_word0_done:
	shr eax, 16
	mov [rdi + 6], ax		; MAC bytes 0-1

	; Read word 1 (MAC bytes 2-3)
	mov eax, (0x01 << 8) | 1
	mov [rsi + E1000_EERD], eax
	mov ecx, 10000
net_e1000_eeprom_wait1:
	pause
	mov eax, [rsi + E1000_EERD]
	test eax, (1 << 4)
	jnz net_e1000_eeprom_word1_done
	dec ecx
	jnz net_e1000_eeprom_wait1
	stc
	jmp net_e1000_eeprom_done

net_e1000_eeprom_word1_done:
	shr eax, 16
	mov [rdi + 8], ax		; MAC bytes 2-3

	; Read word 2 (MAC bytes 4-5)
	mov eax, (0x02 << 8) | 1
	mov [rsi + E1000_EERD], eax
	mov ecx, 10000
net_e1000_eeprom_wait2:
	pause
	mov eax, [rsi + E1000_EERD]
	test eax, (1 << 4)
	jnz net_e1000_eeprom_word2_done
	dec ecx
	jnz net_e1000_eeprom_wait2
	stc
	jmp net_e1000_eeprom_done

net_e1000_eeprom_word2_done:
	shr eax, 16
	mov [rdi + 10], ax		; MAC bytes 4-5
	clc				; Success

net_e1000_eeprom_done:
	pop rax				; EVOLVED Gen-6: removed unused pop rbx
	pop rcx
	ret
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; net_e1000_config - Configure packet buffer base for RX descriptors
;  IN:	RAX = Base address for RX packet buffers
;	RDX = Pointer to net_table entry for this interface
; OUT:	Nothing
net_e1000_config:
	push rdi
	push rcx
	push rax

	mov rdi, [rdx + nt_rx_desc]	; RX descriptor ring base
	mov ecx, E1000_NUM_RX_DESC
	call os_virt_to_phys		; Convert virtual address
net_e1000_config_next:
	stosq				; Store buffer address
	add rdi, 8			; Skip status/length QWORD
	add rax, E1000_RX_BUF_SIZE
	dec ecx
	jnz net_e1000_config_next

	pop rax
	pop rcx
	pop rdi
	ret
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; net_e1000_transmit - Transmit a packet via an Intel e1000 NIC
;  IN:	RSI = Location of packet (physical address)
;	RDX = Pointer to net_table entry for this interface
;	RCX = Length of packet
; OUT:	Nothing
net_e1000_transmit:
	push r8
	push rdi
	push rdx
	push rbx
	push rax

	mov r8, rdx			; R8 = net_table entry

	; Get current TX tail index
	; EVOLVED Gen-6: removed dead xor eax before mov eax
	mov eax, [r8 + nt_tx_tail]
	mov ebx, eax			; EBX = current tail index

	; Calculate descriptor address: base + (tail * 16)
	shl eax, 4
	add rax, [r8 + nt_tx_desc]
	mov rdi, rax			; RDI = pointer to TX descriptor

	; Fill TX descriptor (legacy format)
	; Bytes 0-7: Buffer Address
	mov rax, rsi
	stosq
	; EVOLVED Gen-6: partial register fixes — use 32-bit ops
	; Bytes 8-9: Length
	movzx eax, cx			; EVOLVED Gen-6: movzx replacing partial mov ax,cx
	stosw
	; Byte 10: CSO (Checksum Offset) = 0
	xor eax, eax			; EVOLVED Gen-6: 32-bit xor avoids partial reg merge
	stosb
	; Byte 11: CMD = EOP | IFCS | RS
	mov al, E1000_TXD_CMD_EOP | E1000_TXD_CMD_IFCS | E1000_TXD_CMD_RS
	stosb
	; Byte 12: Status = 0 (will be set by hardware)
	xor eax, eax			; EVOLVED Gen-6: 32-bit xor
	stosb
	; Byte 13: CSS (Checksum Start) = 0
	stosb
	; Bytes 14-15: Special = 0
	stosw				; EAX already 0 from above

	; Advance tail index (wrap at E1000_NUM_TX_DESC)
	inc ebx
	and ebx, E1000_NUM_TX_DESC - 1
	mov [r8 + nt_tx_tail], ebx

	; Write new tail to hardware TDT register to trigger send
	mov rdi, [r8 + nt_base]
	mov [rdi + E1000_TDT], ebx

	; Wait for transmit to complete (poll DD bit in descriptor status)
	; EVOLVED Gen-6: removed dead sub rdi,4 (immediately overwritten)
	mov rdi, [r8 + nt_tx_desc]
	mov eax, [r8 + nt_tx_tail]
	dec eax				; Point to descriptor we just wrote
	and eax, E1000_NUM_TX_DESC - 1
	shl eax, 4
	add rdi, rax
	add rdi, 12			; Offset to status byte
	mov ecx, 1000000		; Timeout counter
net_e1000_transmit_wait:
	pause
	mov al, [rdi]
	test al, E1000_TXD_STAT_DD
	jnz net_e1000_transmit_done
	dec ecx
	jnz net_e1000_transmit_wait

net_e1000_transmit_done:
	pop rax
	pop rbx
	pop rdx
	pop rdi
	pop r8
	ret
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; net_e1000_poll - Poll the Intel e1000 NIC for a received packet
;  IN:	RDX = Pointer to net_table entry for this interface
; OUT:	RDI = Location of packet data
;	RCX = Length of packet (0 if no data)
net_e1000_poll:
	push r8
	push rsi
	push rbx
	push rax

	mov r8, rdx			; R8 = net_table entry
	xor ecx, ecx			; Default: no data

	; Get current RX head index
	mov eax, [r8 + nt_rx_head]
	mov ebx, eax			; EBX = head index

	; Calculate descriptor address: base + (head * 16)
	shl eax, 4
	add rax, [r8 + nt_rx_desc]
	mov rsi, rax			; RSI = pointer to RX descriptor
	prefetchnta [rsi]		; EVOLVED Gen-6: prefetch descriptor to L1

	; Check DD (Descriptor Done) bit in status
	mov al, [rsi + 12]		; Status byte is at offset 12
	test al, E1000_RXD_STAT_DD
	jz net_e1000_poll_nodata	; No packet available

	; Packet received — get buffer address and length
	mov rdi, [rsi]			; Buffer address (bytes 0-7)
	movzx ecx, word [rsi + 8]	; Length (bytes 8-9)

	; Clear the descriptor status for reuse
	mov qword [rsi + 8], 0		; Clear length/status/errors

	; Advance RX head
	inc ebx
	and ebx, E1000_NUM_RX_DESC - 1
	mov [r8 + nt_rx_head], ebx

	; Update hardware RDT (give descriptor back to hardware)
	; Set RDT to the descriptor we just consumed (one behind head)
	mov rsi, [r8 + nt_base]
	mov eax, ebx
	dec eax
	and eax, E1000_NUM_RX_DESC - 1
	mov [rsi + E1000_RDT], eax

net_e1000_poll_nodata:
	pop rax
	pop rbx
	pop rsi
	pop r8
	ret
; -----------------------------------------------------------------------------


; =============================================================================
; EOF
