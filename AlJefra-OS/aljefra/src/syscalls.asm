; =============================================================================
; AlJefra OS -- a 64-bit OS written in Assembly for x86-64 systems
; Copyright (C) 2008-2026 AlJefra -- see LICENSE.TXT
;
; System Call Section -- Accessible to user programs
; =============================================================================


%include "syscalls/bus.asm"
%include "syscalls/debug.asm"
%include "syscalls/nvs.asm"
%include "syscalls/io.asm"
%include "syscalls/net.asm"
%include "syscalls/smp.asm"
%include "syscalls/gpu.asm"
%include "syscalls/evolve.asm"
%include "syscalls/ai_scheduler.asm"
%include "syscalls/system.asm"


; =============================================================================
; EOF
