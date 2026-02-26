; =============================================================================
; AlJefra OS -- a 64-bit OS written in Assembly for x86-64 systems
; Copyright (C) 2008-2026 AlJefra -- see LICENSE.TXT
;
; Debug Functions
; =============================================================================


; -----------------------------------------------------------------------------
; os_debug_dump_(rax|eax|ax|al) -- Dump content of RAX, EAX, AX, or AL
;  IN:	RAX = content to dump
; OUT:	Nothing, all registers preserved
os_debug_dump_rax:
	rol rax, 8
	call os_debug_dump_al
	rol rax, 8
	call os_debug_dump_al
	rol rax, 8
	call os_debug_dump_al
	rol rax, 8
	call os_debug_dump_al
	rol rax, 32
os_debug_dump_eax:			; RAX is used here instead of EAX to preserve the upper 32-bits
	rol rax, 40
	call os_debug_dump_al
	rol rax, 8
	call os_debug_dump_al
	rol rax, 16
os_debug_dump_ax:
	rol ax, 8
	call os_debug_dump_al
	rol ax, 8
; EVOLVED: Use lookup table for hex conversion (branchless, ~2x faster)
; Old version used 4 branches per byte (2 nibbles x cmp+jb)
; New version uses direct table lookup: zero branches, constant time
os_debug_dump_al:
	push rax
	push rbx
	movzx ebx, al
	shr ebx, 4			; High nibble (32-bit shift avoids partial-reg stall)
	mov al, [os_hex_table + rbx]	; EVOLVED: Direct table lookup, no branches
	mov [tchar+0], al
	movzx ebx, byte [rsp+8]	; Reload original AL from stack
	and bl, 0x0F			; Low nibble index
	mov al, [os_hex_table + rbx]	; EVOLVED: Direct table lookup, no branches
	mov [tchar+1], al
	pop rbx
	pop rax
	push rsi
	push rcx
	mov esi, tchar
	mov rcx, 2
	call b_output
	pop rcx
	pop rsi
	ret

; EVOLVED: Hex lookup table - eliminates all branch mispredictions in hex dump
os_hex_table: db '0123456789ABCDEF'
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; os_debug_dump_mem -- Dump content of memory in hex format
;  IN:	RSI = starting address of memory to dump
;	RCX = number of bytes
; OUT:	Nothing, all registers preserved
os_debug_dump_mem:
	push rsi
	push rcx			; Counter
	push rdx			; Total number of bytes to display
	push rax

	test rcx, rcx			; Bail out if no bytes were requested
	jz os_debug_dump_mem_done

	push rsi			; Output '0x'
	push rcx
	mov esi, os_debug_dump_mem_chars
	mov rcx, 2
	call b_output
	pop rcx
	pop rsi

	mov rax, rsi			; Output the memory address
	call os_debug_dump_rax
	call os_debug_newline

nextline:
	xor edx, edx			; EVOLVED Gen-8: xor replacing mov-0
nextchar:
	test rcx, rcx			; EVOLVED Gen-8: test replacing cmp-0
	jz os_debug_dump_mem_done_newline
	push rsi			; Output ' '
	push rcx
	mov esi, os_debug_dump_mem_chars+3
	mov rcx, 1
	call b_output
	pop rcx
	pop rsi
	lodsb
	call os_debug_dump_al
	dec rcx
	inc edx				; EVOLVED Gen-9: 32-bit inc (avoids REX prefix, upper bits known 0)
	cmp edx, 16			; EVOLVED Gen-8: 32-bit cmp (end of line?)
	jne nextchar
	call os_debug_newline
	test rcx, rcx			; EVOLVED Gen-8: test replacing cmp-0
	jz os_debug_dump_mem_done
	jmp nextline

os_debug_dump_mem_done_newline:
	call os_debug_newline

os_debug_dump_mem_done:
	pop rax
	pop rcx
	pop rdx
	pop rsi
	ret

os_debug_dump_mem_chars: db '0x: '
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; os_debug_newline -- Output a newline
;  IN:	Nothing
; OUT:	Nothing, all registers preserved
os_debug_newline:
	push rsi
	push rcx
	mov esi, newline
	mov rcx, 2
	call b_output
	pop rcx
	pop rsi
	ret
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; os_debug_space -- Output a space
;  IN:	Nothing
; OUT:	Nothing, all registers preserved
os_debug_space:
	push rsi
	push rcx
	mov esi, space
	mov rcx, 1
	call b_output
	pop rcx
	pop rsi
	ret
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; os_debug_string - Dump a string to output
; IN:	RSI = String Address (null terminated)
; EVOLVED: Faster string length with bounded scan (max 4096 chars)
; Old version used repne scasb with rcx=-1 which scans up to 2^64 bytes
; on a malformed string. New version bounds the scan and uses simpler loop.
os_debug_string:
	push rdi
	push rcx
	push rax

	xor ecx, ecx
	mov rdi, rsi
.strlen_loop:
	cmp byte [rdi], 0		; Check for null terminator
	je .strlen_done
	inc rdi
	inc ecx
	cmp ecx, 4096			; EVOLVED: Bounded scan prevents runaway
	jb .strlen_loop
.strlen_done:

	call b_output_serial

	pop rax
	pop rcx
	pop rdi
	ret
; -----------------------------------------------------------------------------


%ifndef NO_LFB
; -----------------------------------------------------------------------------
; os_debug_block - Create a block (8x8 pixels) of colour on the screen
; IN:	EBX = Index #
os_debug_block:
	push rax
	push rbx
	push rcx
	push rdx
	push rdi

	; Calculate parameters
	push rbx
	push rax
	xor edx, edx
	xor eax, eax
	xor ebx, ebx
	mov ax, [os_screen_y]		; Screen Y
	add ax, 16			; Lower row
	shr ax, 1			; Quick divide by 2
	mov bx, [os_screen_x]		; Screen X
	shl ebx, 2			; Quick multiply by 4
	imul eax, ebx			; EVOLVED Gen-9: 2-op imul (avoids EDX clobber, 2 uops vs 3)
	mov rdi, [os_screen_lfb]	; Frame buffer base
	add rdi, rax			; Offset is ((screeny - 8) / 2 + screenx * 4)
	pop rax
	pop rbx
	xor edx, edx
	mov dx, [os_screen_ppsl]	; PixelsPerScanLine
	shl edx, 2			; Quick multiply by 4 for line offset
	xor ecx, ecx
	mov cx, [os_screen_x]		; Screen X
	shr ecx, 4			; EVOLVED Gen-9: 32-bit shift (avoids partial register stall)
	sub ecx, 8			; EVOLVED Gen-9: 32-bit sub (avoids partial register stall)
	add ebx, ecx
	shl ebx, 5			; Quick multiply by 32 (8 pixels by 4 bytes each)
	add rdi, rbx
	sub rdi, 16			; Move left by half a box width (4 pixels by 4 bytes each)

	; Draw the 8x8 pixel block
	mov ebx, 8			; 8 pixels tall
	mov eax, 0x00F7CA54		; AlJefra Yellow/Orange
os_debug_block_nextline:
	mov ecx, 8			; 8 pixels wide
	rep stosd
	add rdi, rdx			; Add line offset
	sub rdi, 8*4			; 8 pixels by 4 bytes each
	dec ebx
	jnz os_debug_block_nextline

	pop rdi
	pop rdx
	pop rcx
	pop rbx
	pop rax
	ret
; -----------------------------------------------------------------------------
%endif


; =============================================================================
; EOF
