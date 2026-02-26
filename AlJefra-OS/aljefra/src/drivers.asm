; =============================================================================
; AlJefra OS -- a 64-bit OS written in Assembly for x86-64 systems
; Copyright (C) 2008-2026 AlJefra -- see LICENSE.TXT
;
; Driver Includes
; =============================================================================


; Internal
%include "drivers/apic.asm"
%include "drivers/ioapic.asm"
%include "drivers/msi.asm"
%include "drivers/ps2.asm"
%include "drivers/serial.asm"
%include "drivers/timer.asm"
%include "drivers/virtio.asm"

; Bus
%include "drivers/bus/pcie.asm"
%include "drivers/bus/pci.asm"

; Non-volatile Storage
%include "drivers/nvs/virtio-blk.asm"

; Network
%include "drivers/net/virtio-net.asm"
%include "drivers/net/e1000.asm"

; GPU (NVIDIA RTX 5090 / Blackwell)
%ifndef NO_GPU
%include "drivers/gpu/nvidia.asm"
%endif

; Video
%ifndef NO_LFB
%include "drivers/lfb/lfb.asm"
%endif
%ifndef NO_VGA
%include "drivers/vga.asm"
%endif

NIC_DeviceVendor_ID:	; The supported list of NICs

; Virtio
%ifndef NO_VIRTIO
dw 0x1AF4		; Driver ID
dw 0x1AF4		; Vendor ID
dw 0x1000		; Device ID - legacy
dw 0x1041		; Device ID - v1.0
dw 0x0000
%endif

; Intel e1000/e1000e
dw 0x8086		; Driver ID
dw 0x8086		; Vendor ID
dw 0x100E		; 82540EM (QEMU default e1000)
dw 0x100F		; 82545EM
dw 0x10D3		; 82574L (QEMU e1000e)
dw 0x153A		; I217-LM
dw 0x15B7		; I219-LM
dw 0x15B8		; I219-V (common on modern laptops)
dw 0x156F		; I219-LM (Skylake)
dw 0x0000

; End of list
dw 0x0000
dw 0x0000


; =============================================================================
; EOF
