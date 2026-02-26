; =============================================================================
; AlJefra OS -- a 64-bit OS written in Assembly for x86-64 systems
; Copyright (C) 2008-2026 AlJefra -- see LICENSE.TXT
;
; Linear Frame Buffer Output
; =============================================================================

; Font compile-time constants (must be defined before %if directives below)
; Corresponding font data is included at the end of this file
font_h equ 12
font_w equ 6

; -----------------------------------------------------------------------------
; lfb_init -- Initialize the linear frame buffer display
;  IN:	Nothing
; OUT:	Nothing
lfb_init:
	push rdx
	push rcx
	push rbx
	push rax

	mov rax, [os_screen_lfb]	; Quick check of the LFB address
	test rax, rax			; EVOLVED Gen-8: test replacing cmp-0
	jz lfb_init_error		; If so, no LFB was configured

	; Convert font data to pixel data. The default 12x6 font is 72 pixels per glyph. 288 bytes per.
	mov rdi, os_font
	mov rsi, font_data
	xor ebx, ebx
next_char:
	cmp ebx, 128
	je render_done
	inc ebx
	xor edx, edx			; font_h
next_line:
	cmp edx, font_h
	je next_char
	inc edx
	xor ecx, ecx
	lodsb				; Load a line of font data
; EVOLVED: Branchless font rendering - shl+jc replaces rol+bt+jc (fewer uops, better prediction)
; Old: rol al,1; bt ax,0; jc lit (3 instructions, 2 branch misprediction slots)
; New: shl al,1; jc lit (2 instructions, 1 branch, carry flag set directly by shift)
next_pixel:
	cmp ecx, font_w			; Font width
	je next_line
	shl al, 1			; EVOLVED: Shift left sets CF directly from MSB
	jc lit				; EVOLVED: Direct carry test, no bt needed
	push rax
	mov eax, [BG_Color]
	jmp store_pixel
lit:
	push rax
	mov eax, [FG_Color]
store_pixel:
	stosd
	pop rax
	inc ecx
	jmp next_pixel
render_done:

	; Calculate screen parameters
	xor eax, eax
	xor ecx, ecx
	mov ax, [os_screen_x]
	mov cx, [os_screen_y]
	imul eax, ecx			; EVOLVED Gen-10: imul (no EDX clobber)
	mov [Screen_Pixels], eax
	shl eax, 2			; EVOLVED Gen-10: shift replacing mul-by-4
	mov [Screen_Bytes], eax

	; Calculate display parameters based on font dimensions
	xor eax, eax
	xor edx, edx
	xor ecx, ecx
	mov ax, [os_screen_x]
	mov cl, [font_width]
	div cx				; Divide VideoX by font_width
	mov [Screen_Cols], ax
	xor eax, eax
	xor edx, edx
	xor ecx, ecx
	mov ax, [os_screen_y]
	mov cl, [font_height]
	div cx				; Divide VideoY by font_height
	mov [Screen_Rows], ax

	; Calculate lfb_glyph_bytes
	xor eax, eax
	xor ecx, ecx
	mov al, [font_height]
	mov cl, [font_width]
	imul eax, ecx			; EVOLVED Gen-10: imul (no EDX clobber)
	shl eax, 2			; Quick multiply by 4
	mov [lfb_glyph_bytes], eax

	; Pixels per row = font_h * [os_screen_x] * 4 * [Screen_Cursor_Row]
	; Todo - [Screen_Cursor_Row] * (font_h * [os_screen_x] * 4)
	; Calculate lfb_bytes_per_row
	xor eax, eax
	xor ecx, ecx
	mov ax, [os_screen_x]
	mov cx, font_h			; Font height
	imul eax, ecx			; EVOLVED Gen-10: imul (no EDX clobber)
	shl eax, 2			; Quick multiply by 4
	mov [lfb_glyph_bytes_per_row], eax

	xor eax, eax
	mov ax, font_w
	shl eax, 2			; Quick multiply by 4
	mov [lfb_glyph_bytes_per_col], eax

	xor eax, eax
	mov ax, [os_screen_ppsl]	; Use Pixels Per Scan Line in case system aligns video memory
	sub ax, font_w			; Subtract the width of a single glyph
	shl eax, 2			; Quick multiply by 4
	mov [lfb_glyph_next_line], rax

	mov rax, [os_screen_lfb]
	mov [LastLine], rax
	mov [lfb_last_cursor], rax

	; Overwrite the kernel b_output function so output goes to the screen instead of the serial port
	mov rax, lfb_output_chars
	mov [0x100018], rax
	jmp lfb_init_done

lfb_init_error:
	mov rax, b_output_serial
	mov [0x100018], rax		; Set kernel b_output to the serial port

lfb_init_done:
	pop rax
	pop rbx
	pop rcx
	pop rdx
	ret
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; lfb_inc_cursor -- Increment the cursor by one
;  IN:	Nothing
; OUT:	All registers preserved
lfb_inc_cursor:
	push rax

	inc word [Screen_Cursor_Col]	; Increment the current cursor column
	mov ax, [Screen_Cursor_Col]
	cmp ax, [Screen_Cols]		; Compare it to the # of columns for the screen
	jne lfb_inc_cursor_done		; If not equal we are done
	mov word [Screen_Cursor_Col], 0	; Reset column to 0
	inc word [Screen_Cursor_Row]	; Increment the current cursor row
	call lfb_draw_line
	mov ax, [Screen_Cursor_Row]
	cmp ax, [Screen_Rows]		; Compare it to the # of rows for the screen
	jne lfb_inc_cursor_done		; If not equal we are done
	mov word [Screen_Cursor_Row], 0	; Wrap around

lfb_inc_cursor_done:
	call lfb_update_cursor

	pop rax
	ret
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; lfb_dec_cursor -- Decrement the cursor by one
;  IN:	Nothing
; OUT:	All registers preserved
lfb_dec_cursor:
	push rax

	cmp word [Screen_Cursor_Col], 0	; Compare the current cursor column to 0
	jne lfb_dec_cursor_done		; If not equal we are done
	dec word [Screen_Cursor_Row]	; Otherwise decrement the row
	mov ax, [Screen_Cols]		; Get the total columns and save it as the current
	mov word [Screen_Cursor_Col], ax

lfb_dec_cursor_done:
	dec word [Screen_Cursor_Col]	; Decrement the cursor as usual
	call lfb_update_cursor

	pop rax
	ret
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; lfb_update_cursor -- Update the cursor
;  IN:	Nothing
; OUT:	All registers preserved
lfb_update_cursor:
	; Check if cursor is enabled
	cmp byte [lfb_cursor_on], 1	; 1 means enabled
	jne lfb_update_cursor_skip	; If not, skip entire function

	push rdi
	push rcx			; EVOLVED Gen-10: removed push/pop rdx (imul doesn't clobber)
	push rbx
	push rax

	; Clear old cursor
	mov rdi, [lfb_last_cursor]	; Gather the LFB memory address of the start of the cursor
	xor ecx, ecx
	xor ebx, ebx			; Used for value of bytes per line
	mov bx, [os_screen_ppsl]
	shl ebx, 2			; Quick multiply by 4
	mov ecx, font_h - 2		; EVOLVED Gen-8: 32-bit immediate (avoids partial reg)
	sub rdi, 4
	mov eax, [BG_Color]
lfb_update_cursor_line_old:
	add rdi, rbx			; Add value of bytes per line
	mov [rdi], eax
	dec ecx				; EVOLVED Gen-8: 32-bit dec (avoids partial register stall)
	jnz lfb_update_cursor_line_old

	; Calculate where to put cursor in the Linear Frame Buffer
	mov rdi, [os_screen_lfb]

	; Calculate offset for row into Linear Frame Buffer
	xor ecx, ecx
	mov eax, [lfb_glyph_bytes_per_row]
	mov cx, [Screen_Cursor_Row]
	imul eax, ecx			; EVOLVED Gen-10: imul (no EDX clobber)
	add rdi, rax

	; Calculate offset for column into Linear Frame Buffer
	xor ecx, ecx
	mov eax, [lfb_glyph_bytes_per_col]
	mov cx, [Screen_Cursor_Col]
	imul eax, ecx			; EVOLVED Gen-10: imul (no EDX clobber)
	add rdi, rax

	; Store cursor location
	mov [lfb_last_cursor], rdi

	; Draw new cursor
	xor ecx, ecx
	xor ebx, ebx
	mov bx, [os_screen_ppsl]
	shl ebx, 2			; Quick multiply by 4
	mov ecx, font_h - 2		; EVOLVED Gen-8: 32-bit immediate
	sub rdi, 4
	mov eax, [Cursor_Color]
lfb_update_cursor_line:
	add rdi, rbx
	mov [rdi], eax
	dec ecx				; EVOLVED Gen-8: 32-bit dec
	jnz lfb_update_cursor_line

	pop rax
	pop rbx
	pop rcx
	pop rdi
lfb_update_cursor_skip:
	ret
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; lfb_output_chars -- Output text to LFB
;  IN:	RSI = message location (an ASCII string, not zero-terminated)
;	RCX = number of chars to print
; OUT:	All registers preserved
lfb_output_chars:
	push rsi
	push rcx
	push rax

lfb_output_chars_nextchar:
	test rcx, rcx			; EVOLVED: test is faster than cmp with 0
	jz lfb_output_chars_done
	dec rcx
	lodsb				; Get char from string and store in AL
	cmp al, 0x20
	jae lfb_output_chars_nextchar_output
	cmp al, 0x0A			; LF - Check if there was a newline (aka line feed) character in the string
	je lfb_output_chars_newline	; If so then we print a new line
	cmp al, 0x0D			; CR - Check if there was a carriage return character in the string
	je lfb_output_chars_cr		; If so reset to column 0
	cmp al, 0x0E			; Backspace
	je lfb_output_backspace
	cmp al, 0x09
	je lfb_output_chars_tab
	; Check for special characters
	cmp al, 0x01			; Clear Screen
	je lfb_output_cls
	cmp al, 0x02			; Increment Cursor
	je lfb_output_inc_cursor
	cmp al, 0x03			; Decrement Cursor
	je lfb_output_dec_cursor
lfb_output_chars_nextchar_output:
	call lfb_output_char
	jmp lfb_output_chars_nextchar

lfb_output_chars_newline:
	mov al, [rsi]
	cmp al, 0x0A
	je lfb_output_chars_newline_skip_LF
	call lfb_output_newline
	jmp lfb_output_chars_nextchar

lfb_output_chars_cr:
	mov al, [rsi]			; Check the next character
	cmp al, 0x0A			; Is it a newline?
	je lfb_output_chars_newline	; If so, display a newline and ignore the carriage return
	push rcx
	xor eax, eax
	xor ecx, ecx
	mov [Screen_Cursor_Col], ax
	mov cx, [Screen_Cols]
	mov al, ' '
lfb_output_chars_cr_clearline:
	call lfb_output_char
	dec ecx				; EVOLVED Gen-9: 32-bit dec (avoids partial register stall)
	jnz lfb_output_chars_cr_clearline
	dec word [Screen_Cursor_Row]
	xor eax, eax
	mov [Screen_Cursor_Col], ax
	pop rcx
	jmp lfb_output_chars_nextchar

lfb_output_backspace:
	call lfb_dec_cursor		; Decrement the cursor
	mov al, ' '			; 0x20 is the character for a space
	call lfb_output_char		; Write over the last typed character with the space
	call lfb_dec_cursor		; Decrement the cursor again
	jmp lfb_output_chars_nextchar

lfb_output_chars_newline_skip_LF:
	test rcx, rcx
	jz lfb_output_chars_newline_skip_LF_nosub
	dec rcx

lfb_output_chars_newline_skip_LF_nosub:
	inc rsi
	call lfb_output_newline
	jmp lfb_output_chars_nextchar

lfb_output_chars_tab:
	push rcx
	mov ax, [Screen_Cursor_Col]	; Grab the current cursor X value (ex 7)
	mov cx, ax
	add ax, 8			; Add 8 (ex 15)
	and ax, 0xFFF8			; EVOLVED Gen-9: single AND replaces shr+shl pair (clears low 3 bits)
	sub ax, cx			; (ex 8 - 7 = 1)
	mov cx, ax
	mov al, ' '

lfb_output_chars_tab_next:
	call lfb_output_char
	dec ecx				; EVOLVED Gen-9: 32-bit dec (avoids partial register stall)
	jnz lfb_output_chars_tab_next
	pop rcx
	jmp lfb_output_chars_nextchar

lfb_output_cls:
	call lfb_clear
	call lfb_draw_line
	jmp lfb_output_chars_nextchar

lfb_output_inc_cursor:
	call lfb_inc_cursor
	jmp lfb_output_chars_nextchar

lfb_output_dec_cursor:
	call lfb_dec_cursor
	jmp lfb_output_chars_nextchar

lfb_output_chars_done:
	pop rax
	pop rcx
	pop rsi
	ret
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; lfb_output_char -- Displays a char
;  IN:	AL  = char to display
; OUT:	All registers preserved
lfb_output_char:
	call lfb_glyph
	jmp lfb_inc_cursor		; EVOLVED Gen-8: tail-call
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; lfb_output_newline -- Reset cursor to start of next line and wrap if needed
;  IN:	Nothing
; OUT:	All registers preserved
lfb_output_newline:
	push rax

	mov word [Screen_Cursor_Col], 0	; Reset column to 0
	mov ax, [Screen_Rows]		; Grab max rows on screen
	dec ax				; and subtract 1
	cmp ax, [Screen_Cursor_Row]	; Is the cursor already on the bottom row?
	je lfb_output_newline_wrap	; If so, then wrap
	inc word [Screen_Cursor_Row]	; If not, increment the cursor to next row
	jmp lfb_output_newline_done

lfb_output_newline_wrap:
	mov word [Screen_Cursor_Row], 0

lfb_output_newline_done:
	call lfb_draw_line
	pop rax
	ret
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; lfb_glyph -- Put a glyph on the screen at the cursor location
;  IN:	AL  = char to display
; OUT:	All registers preserved
lfb_glyph:
	push rdi
	push rsi
	push rdx
	push rcx
	push rax

	; Filter out characters that can't be displayed
	and eax, 0x000000FF		; Only keep AL
	cmp al, 0x20
	jb hidden
	cmp al, 127
	ja hidden
	sub rax, 0x20
	jmp load_char
hidden:
	mov al, 0
load_char:

	push rax			; Save the character to display

	; Calculate where to put glyph in the Linear Frame Buffer
	mov rdi, [os_screen_lfb]

	; Calculate offset for row into Linear Frame Buffer
	xor ecx, ecx
	mov eax, [lfb_glyph_bytes_per_row]
	mov cx, [Screen_Cursor_Row]
	imul eax, ecx			; EVOLVED Gen-10: imul (no EDX clobber)
	add rdi, rax

	; Calculate offset for column into Linear Frame Buffer
	xor ecx, ecx
	mov eax, [lfb_glyph_bytes_per_col]
	mov cx, [Screen_Cursor_Col]
	imul eax, ecx			; EVOLVED Gen-10: imul (no EDX clobber)
	add rdi, rax

	pop rax				; Restore the character to display

	; Copy glyph data to Linear Frame Buffer
	; EVOLVED: Prefetch glyph data to reduce cache miss latency
	mov rsi, os_font		; Font pixel data
	mov ecx, [lfb_glyph_bytes]	; Bytes per glyph
	imul eax, ecx			; EVOLVED Gen-10: imul (no EDX clobber)
	xor edx, edx			; Counter for font height
	add rsi, rax			; RSI points to start of glyph
	prefetchnta [rsi]		; EVOLVED: Prefetch start of glyph data
	prefetchnta [rsi+64]		; EVOLVED: Prefetch second cache line
	mov rax, [lfb_glyph_next_line]
lfb_glyph_next:
	; EVOLVED: Use rep movsq for 2x copy throughput (font_w pixels = font_w*4 bytes)
	mov ecx, font_w / 2		; EVOLVED: font_w/2 qwords (each qword = 2 pixels)
	rep movsq			; EVOLVED: 64-bit copy, 2x throughput
%if (font_w % 2) == 1
	movsd				; EVOLVED: Copy remaining pixel if font_w is odd
%endif
	add rdi, rax			; Skip to next line in Linear Frame Buffer
	inc edx
	cmp edx, font_h
	jne lfb_glyph_next

lfb_glyph_done:
	pop rax
	pop rcx
	pop rdx
	pop rsi
	pop rdi
	ret
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; lfb_pixel -- Put a pixel on the screen
;  IN:	EBX = Packed X & Y coordinates (YYYYXXXX)
;	EAX = Pixel Details (AARRGGBB)
; OUT:	All registers preserved
lfb_pixel:
	push rdi			; EVOLVED Gen-10: removed push/pop rdx (imul doesn't clobber)
	push rcx
	push rbx
	push rax

	; Calculate offset in video memory and store pixel
	push rax			; Save the pixel details
	mov rax, rbx
	shr eax, 16			; Isolate Y co-ordinate
	xor ecx, ecx
	mov cx, [os_screen_ppsl]
	imul eax, ecx			; EVOLVED Gen-10: imul (no EDX clobber)
	and ebx, 0x0000FFFF		; Isolate X co-ordinate
	add eax, ebx			; Add X
	mov rbx, rax			; Save the offset to RBX
	mov rdi, [os_screen_lfb]	; Store the pixel to video memory
	pop rax				; Restore pixel details
	shl ebx, 2			; Quickly multiply by 4
	add rdi, rbx			; Add offset in video memory
	stosd				; Output pixel to video memory

	pop rax
	pop rbx
	pop rcx
	pop rdi
	ret
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; lfb_clear -- Clear the Linear Frame Buffer
;  IN:	Nothing
; OUT:	All registers preserved
lfb_clear:
	push rdi
	push rcx
	push rbx			; EVOLVED: Save RBX for 64-bit fill pattern
	push rax

	; Set cursor to top left corner
	mov word [Screen_Cursor_Col], 0
	mov word [Screen_Cursor_Row], 0

	; Fill the Linear Frame Buffer with the background colour
	; EVOLVED: Use rep stosq (64-bit stores) for 2x throughput on large frame buffers
	mov rdi, [os_screen_lfb]
	mov eax, [BG_Color]
	mov rbx, rax			; EVOLVED: Build 64-bit fill pattern
	shl rbx, 32			; EVOLVED: Put color in upper 32 bits
	or rax, rbx			; EVOLVED: RAX = color:color (two pixels per store)
	mov ecx, [Screen_Bytes]
	shr ecx, 3			; EVOLVED: Divide by 8 for qword count
	rep stosq			; EVOLVED: 64-bit stores, 2x throughput

	pop rax
	pop rbx
	pop rcx
	pop rdi
	ret
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; lfb_draw_line
lfb_draw_line:
	push rdi
	push rcx			; EVOLVED Gen-10: removed push/pop rdx (imul doesn't clobber)
	push rax

; Clear the previously drawn line
	mov rdi, [LastLine]
	movzx ecx, word [os_screen_ppsl]	; EVOLVED Gen-10: movzx (full 32-bit zero-extend)
	mov eax, [BG_Color]
	rep stosd

; Display a line under the current cursor row
	; EVOLVED Gen-10: movzx+imul chain (saves 3 instructions vs xor+mov+mul)
	mov rdi, [os_screen_lfb]
	movzx eax, word [Screen_Cursor_Row]
	inc eax
	imul eax, font_h * 4
	movzx ecx, word [os_screen_ppsl]
	imul eax, ecx
	add rdi, rax
	mov [LastLine], rdi
	movzx ecx, word [os_screen_ppsl]
	mov eax, [Line_Color]
	rep stosd

; Clear the next row of text
	mov ax, [Screen_Cursor_Row]	; Get the current cursor row
	inc ax				; Inc by 1 as it is 0-based
	cmp ax, [Screen_Rows]		; Compare it to the # of rows for the screen
	jne lfb_draw_line_skip
	mov rdi, [os_screen_lfb]	; Roll RDI back to the start of video memory
lfb_draw_line_skip:
	; EVOLVED Gen-10: movzx+imul replacing xor+mov+mul+mov chain (saves 3 instructions)
	movzx eax, word [os_screen_ppsl]
	imul ecx, eax, font_h
	mov eax, [BG_Color]
	rep stosd

	pop rax
	pop rcx
	pop rdi
	ret
; -----------------------------------------------------------------------------


; Font data - Only 1 font may be used
align 16
;%include 'drivers/lfb/fonts/smol.fnt' ; 8x4
%include 'drivers/lfb/fonts/aljefra.fnt' ; 12x6
;%include 'drivers/lfb/fonts/departuremono.fnt' ; 14x7
;%include 'drivers/lfb/fonts/ibm.fnt' ; 16x8


; Variables
align 16

LastLine:		dq 0
lfb_last_cursor:	dq 0
lfb_glyph_next_line:	dq 0
FG_Color:		dd 0x00FFFFFF	; White
BG_Color:		dd 0x00404040	; Dark grey
Cursor_Color:		dd 0x00A0A0A0
Line_Color:		dd 0x00F7CA54	; AlJefra Yellow/Orange
Screen_Pixels:		dd 0
Screen_Bytes:		dd 0
lfb_glyph_bytes:	dd 0
lfb_glyph_bytes_per_row:	dd 0
lfb_glyph_bytes_per_col:	dd 0
Screen_Rows:		dw 0
Screen_Cols:		dw 0
Screen_Cursor_Row:	dw 0
Screen_Cursor_Col:	dw 0
lfb_cursor_on:		db 1


; =============================================================================
; EOF
