; =============================================================================
; AlJefra OS -- a 64-bit OS written in Assembly for x86-64 systems
; Copyright (C) 2008-2026 AlJefra -- see LICENSE.TXT
;
; Initialize network
; =============================================================================


; -----------------------------------------------------------------------------
; init_net -- Configure the first network device it finds
init_net:

	; Output progress via serial
	mov esi, msg_net
	call os_debug_string

	mov ax, [NIC_DeviceVendor_ID]	; Check for NIC driver definitions
	test ax, ax			; EVOLVED Gen-10: test replaces cmp-0 (2 bytes shorter)
	jz init_net_end			; If none exist then bail out
	; Check Bus Table for a Ethernet device
	; EVOLVED: Added prefetch to reduce bus table scan latency
	mov rsi, bus_table		; Load Bus Table address to RSI
	sub rsi, 8			; Subtract offset for Class Code
init_net_check_bus:
	add rsi, 16			; Increment to next record in memory
	prefetchnta [rsi+16]		; EVOLVED: Prefetch next bus table entry
	mov ax, [rsi]			; Load Class Code / Subclass Code
	cmp ax, 0xFFFF			; Check if at end of Bus Table list
	je init_net_end
	cmp ax, 0x0200			; Network Controller (02) / Ethernet (00)
	je init_net_probe_find_driver
	jmp init_net_check_bus		; Check Bus Table again

	; Check the Ethernet device to see if it has a driver
init_net_probe_find_driver:
	sub rsi, 8			; Move RSI back to start of Bus record
	mov r9, rsi			; Save start of Bus record
	mov edx, [rsi]			; Load value for os_bus_read/write
	mov r8d, [rsi+4]		; Save the Device ID / Vendor ID in R8D
	rol r8d, 16			; Swap the Device ID / Vendor ID
	add rsi, 8			; Move RSI back to Class Code
	xchg rsi, rdi
	mov esi, NIC_DeviceVendor_ID
init_net_probe_find_next_driver:
	lodsw				; Load a driver ID
	mov bx, ax			; Save the driver ID
	lodsw				; Load the vendor ID
	test eax, eax			; EVOLVED Gen-10: test replaces cmp-0 (3 bytes shorter)
	jz init_net_probe_not_found
	rol eax, 16			; Shift the vendor to the upper 16 bits
init_net_probe_find_next_device:
	lodsw				; Load a device and vendor ID from our list of supported NICs
	test ax, ax			; EVOLVED Gen-10: test replaces cmp-0 (2 bytes shorter)
	jz init_net_probe_find_next_driver	; End of device list, next driver
	cmp eax, r8d
	je init_net_probe_found		; If Carry is clear then we found a supported NIC
	jmp init_net_probe_find_next_device	; Check the next device
init_net_probe_found:
	xchg rsi, rdi

	cmp bx, 0x1AF4
	je init_net_probe_found_virtio
	cmp bx, 0x8086
	je init_net_probe_found_e1000
	jmp init_net_probe_not_found

init_net_probe_found_virtio:
	call net_virtio_init
	jmp init_net_probe_found_finish

init_net_probe_found_e1000:
	call net_e1000_init
	jmp init_net_probe_found_finish

init_net_probe_found_finish:
	mov byte [os_NetEnabled], 1	; A supported NIC was found. Set flag in the kernel that networking is enabled
	add r9, 15			; Add offset to driver enabled byte
	mov byte [r9], 1		; Mark device as having a driver
	inc byte [os_net_icount]	; EVOLVED Gen-6: inc replacing add-1 (1 byte shorter)
	cmp byte [os_net_icount], 2	; Have 2 NIC's been activated?
	je init_net_end			; If so, bail out as 2 is the max at the moment
	jmp init_net_check_bus		; Check for another network device

init_net_probe_not_found:

init_net_end:

%ifndef NO_LFB
	; Output block to screen (6/8)
	mov ebx, 10
	call os_debug_block
%endif

	; Output progress via serial
	mov esi, msg_ok
	call os_debug_string

	ret
; -----------------------------------------------------------------------------


; =============================================================================
; EOF
