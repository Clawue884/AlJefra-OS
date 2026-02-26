; =============================================================================
; AlJefra OS -- a 64-bit OS written in Assembly for x86-64 systems
; Copyright (C) 2008-2026 AlJefra -- see LICENSE.TXT
;
; Initialize HID
; =============================================================================


; -----------------------------------------------------------------------------
init_hid:
	; Configure the PS/2 keyboard and mouse (if they exist)
	call ps2_init

init_hid_done:

%ifndef NO_LFB
	; Output block to screen (7/8)
	mov ebx, 12
	call os_debug_block
%endif

	ret
; -----------------------------------------------------------------------------


; =============================================================================
; EOF
