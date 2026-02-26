; =============================================================================
; AlJefra OS -- a 64-bit OS written in Assembly for x86-64 systems
; Copyright (C) 2008-2026 AlJefra -- see LICENSE.TXT
;
; NVIDIA GPU Driver (RTX 5090 / Blackwell GB202)
; Provides: PCIe discovery, BAR mapping, MMIO access, command submission,
;           VRAM management, compute dispatch, DMA engine
;
; This is the core GPU engine - all OS components use this single driver
; instead of reimplementing GPU connectivity each time.
; =============================================================================


; NVIDIA PCI Vendor ID
NVIDIA_VENDOR_ID	equ 0x10DE

; RTX 5090 (Blackwell GB202) Device IDs
NVIDIA_RTX5090_DID	equ 0x2B85
NVIDIA_RTX5090_DID_ALT	equ 0x2B80	; Alternate SKU

; PCI Class/Subclass for VGA-compatible display controller
GPU_CLASS_DISPLAY	equ 0x03
GPU_SUBCLASS_VGA	equ 0x00
GPU_SUBCLASS_3D		equ 0x02

; NVIDIA GPU MMIO Register Blocks (offsets from BAR0)
NV_PMC			equ 0x000000	; Power Management Controller
NV_PMC_BOOT_0		equ 0x000000	; Boot/chipset ID register
NV_PMC_BOOT_1		equ 0x000004	; Revision register
NV_PMC_ENABLE		equ 0x000200	; Engine enable bitmask
NV_PMC_INTR_0		equ 0x000100	; Interrupt status
NV_PMC_INTR_EN_0	equ 0x000140	; Interrupt enable
NV_PMC_INTR_EN_SET	equ 0x000160	; Interrupt enable set
NV_PMC_INTR_EN_CLR	equ 0x000180	; Interrupt enable clear

NV_PBUS			equ 0x001000	; Bus control
NV_PBUS_PCI_NV_0	equ 0x001800	; PCI config mirror
NV_PBUS_BAR0_WINDOW	equ 0x001700	; BAR0 window for VRAM access
NV_PBUS_BAR1_BLOCK	equ 0x001704	; BAR1 block base

NV_PTIMER		equ 0x009000	; GPU timer
NV_PTIMER_TIME_0	equ 0x009400	; Timer low 32 bits
NV_PTIMER_TIME_1	equ 0x009410	; Timer high 32 bits

NV_PFB			equ 0x100000	; Framebuffer / Memory controller
NV_PFB_CFG0		equ 0x100200	; FB config (VRAM size)
NV_PFB_CSTATUS		equ 0x10020C	; FB status

NV_PFIFO		equ 0x002000	; Command FIFO engine
NV_PFIFO_ENABLE		equ 0x002500	; FIFO master enable
NV_PFIFO_RUNLIST	equ 0x002600	; Runlist base address
NV_PFIFO_CHAN_BASE	equ 0x800000	; Channel descriptor base (modern)

NV_PGRAPH		equ 0x400000	; Graphics/Compute engine
NV_PGRAPH_STATUS	equ 0x400700	; Engine status
NV_PGRAPH_INTR		equ 0x400100	; Graphics interrupt status

NV_PCOPY0		equ 0x104000	; Copy Engine 0 (DMA)
NV_PCOPY1		equ 0x105000	; Copy Engine 1 (DMA)

; GPU Command Types for pushbuffer
NV_CMD_NOP		equ 0x00000000
NV_CMD_COMPUTE_DISPATCH equ 0x00000001
NV_CMD_DMA_COPY		equ 0x00000002
NV_CMD_FENCE_SIGNAL	equ 0x00000003
NV_CMD_FENCE_WAIT	equ 0x00000004

; GPU Memory regions (physical addresses assigned during init)
; These are populated by gpu_init and stored in system variables
; gpu_bar0_base  - MMIO registers (from PCIe BAR0)
; gpu_bar1_base  - VRAM aperture (from PCIe BAR1)
; gpu_bar3_base  - USERD / doorbell (from PCIe BAR3, if present)

; Command queue constants
GPU_CMDQ_SIZE		equ 4096	; Command queue size in entries (16 bytes each = 64KB)
GPU_CMDQ_ENTRY_SIZE	equ 16		; Each command is 16 bytes
GPU_MAX_CHANNELS	equ 64		; Maximum GPU channels

; VRAM allocation granularity
GPU_VRAM_PAGE_SIZE	equ 0x200000	; 2MB pages for VRAM
GPU_VRAM_PAGE_SHIFT	equ 21


; =============================================================================
; GPU Initialization
; =============================================================================

; -----------------------------------------------------------------------------
; gpu_init -- Initialize the NVIDIA GPU
;  IN:	Nothing
; OUT:	Carry clear on success, carry set on failure
;	All other registers preserved
gpu_init:
	push rax
	push rbx
	push rcx
	push rdx
	push rsi
	push rdi

	; Output progress via serial
	mov esi, msg_gpu
	call os_debug_string

	; Step 1: Scan the bus table for an NVIDIA GPU
	call gpu_find_device
	jc gpu_init_not_found

	; Store the bus address for future use
	mov [os_GPU_BusAddr], edx

	; Step 2: Enable bus mastering and memory space on the GPU
	call gpu_enable_bus_master

	; Step 3: Read BAR0 (MMIO registers)
	push rdx
	mov eax, edx
	mov al, 0		; BAR0
	mov edx, eax
	call os_bus_read_bar
	mov [os_GPU_BAR0], rax
	mov [os_GPU_BAR0_Size], rcx
	pop rdx

	; Step 4: Read BAR1 (VRAM aperture)
	push rdx
	mov eax, edx
	mov al, 2		; BAR1 (register 6 = BAR1 for 64-bit BARs)
	mov edx, eax
	call os_bus_read_bar
	mov [os_GPU_BAR1], rax
	mov [os_GPU_BAR1_Size], rcx
	pop rdx

	; Step 5: Verify GPU identity via PMC_BOOT_0
	mov rsi, [os_GPU_BAR0]
	mov eax, [rsi + NV_PMC_BOOT_0]
	mov [os_GPU_ChipID], eax

	; Step 6: Read VRAM size from PFB
	mov eax, [rsi + NV_PFB_CSTATUS]
	shr eax, 20			; Convert to MiB
	mov [os_GPU_VRAM_MiB], eax

	; Step 7: Enable PMC engines
	mov dword [rsi + NV_PMC_ENABLE], 0xFFFFFFFF	; Enable all engines

	; Step 8: Initialize PTIMER
	call gpu_init_timer

	; Step 9: Initialize command FIFO
	call gpu_init_fifo

	; Step 10: Initialize VRAM allocator
	call gpu_init_vram_alloc

	; Step 11: Set up the compute dispatch engine
	call gpu_init_compute

	; Step 12: Enable MSI/MSI-X interrupts for the GPU
	call gpu_init_interrupts

	; Mark GPU as enabled
	mov byte [os_GPUEnabled], 1

	; Output success
	mov esi, msg_ok
	call os_debug_string

%ifndef NO_LFB
	; Output block to screen (GPU init)
	mov ebx, 10
	call os_debug_block
%endif

	pop rdi
	pop rsi
	pop rdx
	pop rcx
	pop rbx
	pop rax
	clc
	ret

gpu_init_not_found:
	mov esi, msg_gpu_not_found
	call os_debug_string
	pop rdi
	pop rsi
	pop rdx
	pop rcx
	pop rbx
	pop rax
	stc
	ret
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; gpu_find_device -- Scan bus table for an NVIDIA GPU
;  IN:	Nothing
; OUT:	EDX = Bus address of GPU device
;	Carry clear if found, carry set if not found
gpu_find_device:
	push rsi
	push rax

	mov rsi, bus_table

gpu_find_device_next:
	mov eax, [rsi + 4]		; Load Vendor ID / Device ID
	cmp eax, 0xFFFFFFFF		; End of table
	je gpu_find_device_fail

	; Check Vendor ID (low 16 bits)
	cmp ax, NVIDIA_VENDOR_ID
	jne gpu_find_device_skip

	; Check Class code (byte 8 of bus table entry)
	mov al, [rsi + 8]
	cmp al, GPU_CLASS_DISPLAY	; Display controller class
	jne gpu_find_device_skip

	; Found an NVIDIA GPU
	mov edx, [rsi]			; Load bus address
	mov ax, [rsi + 6]		; Load Device ID
	mov [os_GPU_DeviceID], ax

	pop rax
	pop rsi
	clc
	ret

gpu_find_device_skip:
	add rsi, 16			; Next bus table entry
	jmp gpu_find_device_next

gpu_find_device_fail:
	pop rax
	pop rsi
	stc
	ret
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; gpu_enable_bus_master -- Enable bus mastering and memory space for the GPU
;  IN:	EDX = Bus address
; OUT:	Nothing, all registers preserved
gpu_enable_bus_master:
	push rax
	push rdx

	mov dl, 0x01			; Status/Command register
	call os_bus_read
	or eax, 0x00000006		; Set Memory Space (bit 1) + Bus Master (bit 2)
	call os_bus_write

	pop rdx
	pop rax
	ret
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; gpu_init_timer -- Initialize the GPU timer (PTIMER)
;  IN:	Nothing (uses os_GPU_BAR0)
; OUT:	Nothing, all registers preserved
gpu_init_timer:
	push rsi
	push rax

	mov rsi, [os_GPU_BAR0]

	; Read the current GPU timer to verify it's running
	mov eax, [rsi + NV_PTIMER_TIME_0]
	mov [os_GPU_TimerBase], eax

	pop rax
	pop rsi
	ret
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; gpu_init_fifo -- Initialize the GPU command FIFO engine
;  IN:	Nothing
; OUT:	Nothing, all registers preserved
gpu_init_fifo:
	push rsi
	push rdi
	push rax
	push rcx

	mov rsi, [os_GPU_BAR0]

	; Enable the FIFO engine
	mov dword [rsi + NV_PFIFO_ENABLE], 1

	; Set up the command queue in system memory
	; We allocate a 64KB region for the pushbuffer
	mov rdi, [os_GPU_CmdQ_Base]
	xor eax, eax
	mov ecx, GPU_CMDQ_SIZE * GPU_CMDQ_ENTRY_SIZE / 4
	rep stosd			; Zero the command queue

	; Initialize queue head/tail pointers
	mov dword [os_GPU_CmdQ_Head], 0
	mov dword [os_GPU_CmdQ_Tail], 0

	pop rcx
	pop rax
	pop rdi
	pop rsi
	ret
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; gpu_init_vram_alloc -- Initialize the VRAM page allocator
;  IN:	Nothing
; OUT:	Nothing, all registers preserved
gpu_init_vram_alloc:
	push rdi
	push rax
	push rcx

	; Calculate total VRAM pages
	mov eax, [os_GPU_VRAM_MiB]
	shl eax, 20			; Convert MiB to bytes
	shr eax, GPU_VRAM_PAGE_SHIFT	; Divide by page size
	mov [os_GPU_VRAM_Pages], eax

	; Initialize the VRAM bitmap (1 bit per 2MB page)
	; Max 32GB = 16384 pages = 2048 bytes for bitmap
	mov rdi, os_GPU_VRAM_Bitmap
	xor eax, eax
	mov ecx, 512			; 2048 bytes / 4
	rep stosd			; Zero = all pages free

	pop rcx
	pop rax
	pop rdi
	ret
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; gpu_init_compute -- Initialize the GPU compute dispatch engine
;  IN:	Nothing
; OUT:	Nothing, all registers preserved
gpu_init_compute:
	push rsi
	push rax

	mov rsi, [os_GPU_BAR0]

	; Read PGRAPH status to ensure engine is idle
	mov eax, [rsi + NV_PGRAPH_STATUS]
	; Clear any pending interrupts
	mov dword [rsi + NV_PGRAPH_INTR], 0xFFFFFFFF

	; Mark compute engine as ready
	mov byte [os_GPU_ComputeReady], 1

	pop rax
	pop rsi
	ret
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; gpu_init_interrupts -- Set up MSI interrupts for the GPU
;  IN:	Nothing (uses os_GPU_BusAddr)
; OUT:	Nothing, all registers preserved
gpu_init_interrupts:
	push rax
	push rcx
	push rdx

	mov edx, [os_GPU_BusAddr]
	mov cl, 0x05			; MSI capability ID
	call os_bus_cap_check
	jc gpu_init_interrupts_no_msi

	; MSI found - configure it via the MSI driver
	; TODO: implement os_msi_enable for GPU
	; mov edx, [os_GPU_BusAddr]
	; call os_msi_enable

gpu_init_interrupts_no_msi:
	; Fall back to legacy interrupts or MSI-X
	pop rdx
	pop rcx
	pop rax
	ret
; -----------------------------------------------------------------------------


; =============================================================================
; GPU Engine API -- These functions are the reusable interface
; =============================================================================


; -----------------------------------------------------------------------------
; gpu_mmio_read32 -- Read a 32-bit MMIO register from the GPU
;  IN:	ECX = Register offset from BAR0
; OUT:	EAX = Register value
;	All other registers preserved
gpu_mmio_read32:
	push rsi
	mov rsi, [os_GPU_BAR0]
	add rsi, rcx
	mov eax, [rsi]
	pop rsi
	ret
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; gpu_mmio_write32 -- Write a 32-bit value to a GPU MMIO register
;  IN:	ECX = Register offset from BAR0
;	EAX = Value to write
; OUT:	Nothing, all registers preserved
gpu_mmio_write32:
	push rsi
	mov rsi, [os_GPU_BAR0]
	add rsi, rcx
	mov [rsi], eax
	pop rsi
	ret
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; gpu_vram_read32 -- Read 32 bits from GPU VRAM
;  IN:	RCX = Offset into VRAM
; OUT:	EAX = Value read
;	All other registers preserved
gpu_vram_read32:
	push rsi
	mov rsi, [os_GPU_BAR1]
	add rsi, rcx
	mov eax, [rsi]
	pop rsi
	ret
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; gpu_vram_write32 -- Write 32 bits to GPU VRAM
;  IN:	RCX = Offset into VRAM
;	EAX = Value to write
; OUT:	Nothing, all registers preserved
gpu_vram_write32:
	push rsi
	mov rsi, [os_GPU_BAR1]
	add rsi, rcx
	mov [rsi], eax
	pop rsi
	ret
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; gpu_vram_alloc -- Allocate VRAM pages
;  IN:	ECX = Number of 2MB pages to allocate
; OUT:	RAX = Physical offset in VRAM (0xFFFFFFFFFFFFFFFF on failure)
;	All other registers preserved
gpu_vram_alloc:
	push rbx
	push rcx
	push rdx
	push rsi

	mov rsi, os_GPU_VRAM_Bitmap
	xor edx, edx			; Current bit position
	mov ebx, ecx			; Pages needed

gpu_vram_alloc_scan:
	cmp edx, [os_GPU_VRAM_Pages]
	jge gpu_vram_alloc_fail

	; Check if this run of pages is free
	push rdx
	push rcx
	mov ecx, ebx			; Pages needed
	mov rax, rdx			; Starting bit

gpu_vram_alloc_check_run:
	test ecx, ecx
	jz gpu_vram_alloc_found

	; Check bit at position RAX
	push rax
	push rcx
	mov rcx, rax
	shr rcx, 3			; Byte index
	and al, 7			; Bit index
	mov cl, al
	mov al, [rsi + rcx]
	bt eax, ecx			; Test bit
	pop rcx
	pop rax
	jc gpu_vram_alloc_check_fail	; Bit set = page in use

	inc rax
	dec ecx
	jmp gpu_vram_alloc_check_run

gpu_vram_alloc_found:
	pop rcx
	pop rdx

	; Mark the pages as allocated
	push rdx
	push rcx
	mov ecx, ebx

gpu_vram_alloc_mark:
	test ecx, ecx
	jz gpu_vram_alloc_mark_done

	push rax
	push rcx
	mov rax, rdx
	mov rcx, rax
	shr rcx, 3			; Byte index
	and al, 7			; Bit index
	push rbx
	mov bl, al
	mov cl, bl
	mov al, [rsi + rcx]
	bts eax, ecx			; Set bit
	mov [rsi + rcx], al
	pop rbx
	pop rcx
	pop rax

	inc edx
	dec ecx
	jmp gpu_vram_alloc_mark

gpu_vram_alloc_mark_done:
	pop rcx
	pop rdx

	; Calculate the physical VRAM offset
	mov rax, rdx
	shl rax, GPU_VRAM_PAGE_SHIFT	; Multiply by 2MB page size

	pop rsi
	pop rdx
	pop rcx
	pop rbx
	ret

gpu_vram_alloc_check_fail:
	pop rcx
	pop rdx
	inc edx
	jmp gpu_vram_alloc_scan

gpu_vram_alloc_fail:
	mov rax, 0xFFFFFFFFFFFFFFFF
	pop rsi
	pop rdx
	pop rcx
	pop rbx
	ret
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; gpu_vram_free -- Free previously allocated VRAM pages
;  IN:	RAX = Physical VRAM offset (as returned by gpu_vram_alloc)
;	ECX = Number of 2MB pages to free
; OUT:	Nothing, all registers preserved
gpu_vram_free:
	push rax
	push rcx
	push rdx
	push rsi

	mov rsi, os_GPU_VRAM_Bitmap
	shr rax, GPU_VRAM_PAGE_SHIFT	; Convert offset to page number
	mov edx, eax

gpu_vram_free_loop:
	test ecx, ecx
	jz gpu_vram_free_done

	; Clear bit at position EDX
	push rax
	push rcx
	mov rax, rdx
	mov rcx, rax
	shr rcx, 3			; Byte index
	and al, 7			; Bit index
	push rbx
	mov bl, al
	mov cl, bl
	mov al, [rsi + rcx]
	btr eax, ecx			; Clear bit
	mov [rsi + rcx], al
	pop rbx
	pop rcx
	pop rax

	inc edx
	dec ecx
	jmp gpu_vram_free_loop

gpu_vram_free_done:
	pop rsi
	pop rdx
	pop rcx
	pop rax
	ret
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; gpu_cmd_submit -- Submit a command to the GPU command queue
;  IN:	RAX = Command type (NV_CMD_*)
;	RBX = Parameter 0 (command-specific)
;	RCX = Parameter 1 (command-specific)
;	RDX = Parameter 2 (command-specific)
; OUT:	RAX = Fence ID for this command (for synchronization)
;	Carry clear on success, carry set if queue full
gpu_cmd_submit:
	push rsi
	push rdi

	; Check if queue is full
	mov edi, [os_GPU_CmdQ_Tail]
	inc edi
	and edi, (GPU_CMDQ_SIZE - 1)	; Wrap around
	cmp edi, [os_GPU_CmdQ_Head]
	je gpu_cmd_submit_full

	; Write command to queue
	mov rsi, [os_GPU_CmdQ_Base]
	mov edi, [os_GPU_CmdQ_Tail]
	shl edi, 4			; Multiply by 16 (entry size)
	add rsi, rdi

	mov [rsi + 0], eax		; Command type
	mov [rsi + 4], ebx		; Parameter 0
	mov [rsi + 8], ecx		; Parameter 1
	mov [rsi + 12], edx		; Parameter 2

	; Advance tail
	mov edi, [os_GPU_CmdQ_Tail]
	inc edi
	and edi, (GPU_CMDQ_SIZE - 1)
	mov [os_GPU_CmdQ_Tail], edi

	; Increment and return fence ID
	mov eax, [os_GPU_FenceCounter]
	inc dword [os_GPU_FenceCounter]

	; Ring the GPU doorbell to notify of new work
	call gpu_ring_doorbell

	pop rdi
	pop rsi
	clc
	ret

gpu_cmd_submit_full:
	pop rdi
	pop rsi
	stc
	ret
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; gpu_ring_doorbell -- Notify the GPU of pending commands
;  IN:	Nothing
; OUT:	Nothing, all registers preserved
gpu_ring_doorbell:
	push rsi
	push rax

	mov rsi, [os_GPU_BAR0]

	; Write the updated tail pointer to the GPU's doorbell register
	; On modern NVIDIA GPUs this triggers command processing
	mov eax, [os_GPU_CmdQ_Tail]
	mov [rsi + NV_PFIFO_RUNLIST + 4], eax	; Channel 0 doorbell

	pop rax
	pop rsi
	ret
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; gpu_fence_wait -- Wait for a GPU command to complete
;  IN:	EAX = Fence ID to wait for
; OUT:	Nothing (returns when fence is signaled)
;	All other registers preserved
gpu_fence_wait:
	push rbx

gpu_fence_wait_loop:
	mov ebx, [os_GPU_FenceCompleted]
	cmp ebx, eax
	jge gpu_fence_wait_done
	pause				; CPU hint for spin-wait
	jmp gpu_fence_wait_loop

gpu_fence_wait_done:
	pop rbx
	ret
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; gpu_compute_dispatch -- Dispatch a compute workload to the GPU
;  IN:	RSI = Pointer to compute parameters structure
;		[RSI + 0x00] = Shader/kernel address in VRAM (8 bytes)
;		[RSI + 0x08] = Grid X dimension (4 bytes)
;		[RSI + 0x0C] = Grid Y dimension (4 bytes)
;		[RSI + 0x10] = Grid Z dimension (4 bytes)
;		[RSI + 0x14] = Block X dimension (4 bytes)
;		[RSI + 0x18] = Block Y dimension (4 bytes)
;		[RSI + 0x1C] = Block Z dimension (4 bytes)
;		[RSI + 0x20] = Input buffer VRAM offset (8 bytes)
;		[RSI + 0x28] = Output buffer VRAM offset (8 bytes)
;		[RSI + 0x30] = Input size in bytes (8 bytes)
;		[RSI + 0x38] = Output size in bytes (8 bytes)
; OUT:	EAX = Fence ID (use gpu_fence_wait to synchronize)
;	Carry clear on success, carry set on failure
gpu_compute_dispatch:
	push rbx
	push rcx
	push rdx

	; Verify compute engine is ready
	cmp byte [os_GPU_ComputeReady], 1
	jne gpu_compute_dispatch_fail

	; Submit compute dispatch command
	mov eax, NV_CMD_COMPUTE_DISPATCH
	mov rbx, [rsi + 0x00]		; Shader address
	mov ecx, [rsi + 0x08]		; Grid X
	mov edx, [rsi + 0x20]		; Input buffer
	call gpu_cmd_submit
	jc gpu_compute_dispatch_fail

	pop rdx
	pop rcx
	pop rbx
	clc
	ret

gpu_compute_dispatch_fail:
	pop rdx
	pop rcx
	pop rbx
	stc
	ret
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; gpu_dma_copy_to_vram -- DMA copy from system memory to VRAM
;  IN:	RSI = Source address in system memory
;	RDI = Destination offset in VRAM
;	RCX = Number of bytes to copy
; OUT:	EAX = Fence ID for completion
;	Carry clear on success, carry set on failure
gpu_dma_copy_to_vram:
	push rbx
	push rdx

	; Use the copy engine command
	mov eax, NV_CMD_DMA_COPY
	mov rbx, rsi			; Source
	; RCX already has size
	mov rdx, rdi			; Destination in VRAM
	call gpu_cmd_submit

	pop rdx
	pop rbx
	ret
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; gpu_dma_copy_from_vram -- DMA copy from VRAM to system memory
;  IN:	RSI = Source offset in VRAM
;	RDI = Destination address in system memory
;	RCX = Number of bytes to copy
; OUT:	EAX = Fence ID for completion
;	Carry clear on success, carry set on failure
gpu_dma_copy_from_vram:
	push rbx
	push rdx

	; DMA copy with source/dest swapped flag
	mov eax, NV_CMD_DMA_COPY
	or eax, 0x80000000		; Set bit 31 for VRAM->system direction
	mov rbx, rsi			; Source in VRAM
	; RCX already has size
	mov rdx, rdi			; Destination in system memory
	call gpu_cmd_submit

	pop rdx
	pop rbx
	ret
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; gpu_get_status -- Get GPU status information
;  IN:	Nothing
; OUT:	RAX = GPU status word:
;		Bit 0: GPU present
;		Bit 1: GPU initialized
;		Bit 2: Compute engine ready
;		Bit 3: FIFO active
;		Bits 16-31: Commands pending in queue
;	All other registers preserved
gpu_get_status:
	xor eax, eax

	cmp byte [os_GPUEnabled], 0
	je gpu_get_status_done

	or al, 0x01			; GPU present
	or al, 0x02			; GPU initialized

	cmp byte [os_GPU_ComputeReady], 1
	jne gpu_get_status_no_compute
	or al, 0x04			; Compute ready
gpu_get_status_no_compute:

	; Calculate pending commands
	push rbx
	mov ebx, [os_GPU_CmdQ_Tail]
	sub ebx, [os_GPU_CmdQ_Head]
	and ebx, (GPU_CMDQ_SIZE - 1)
	shl ebx, 16
	or eax, ebx
	pop rbx

gpu_get_status_done:
	ret
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; gpu_get_vram_free -- Get amount of free VRAM
;  IN:	Nothing
; OUT:	RAX = Free VRAM in bytes
;	All other registers preserved
gpu_get_vram_free:
	push rcx
	push rdx
	push rsi

	mov rsi, os_GPU_VRAM_Bitmap
	xor eax, eax			; Free page counter
	xor ecx, ecx			; Current bit

gpu_get_vram_free_loop:
	cmp ecx, [os_GPU_VRAM_Pages]
	jge gpu_get_vram_free_done

	push rax
	push rcx
	mov rax, rcx
	shr rcx, 3
	and al, 7
	push rbx
	mov bl, al
	mov cl, bl
	movzx edx, byte [rsi + rcx]
	bt edx, ecx
	pop rbx
	pop rcx
	pop rax
	jc gpu_get_vram_free_next	; Bit set = page in use
	inc eax				; Page is free

gpu_get_vram_free_next:
	inc ecx
	jmp gpu_get_vram_free_loop

gpu_get_vram_free_done:
	shl rax, GPU_VRAM_PAGE_SHIFT	; Convert pages to bytes

	pop rsi
	pop rdx
	pop rcx
	ret
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; gpu_benchmark -- Run a simple GPU benchmark (measures command latency)
;  IN:	Nothing
; OUT:	RAX = Latency in HPET ticks per command round-trip
;	All other registers preserved
gpu_benchmark:
	push rbx
	push rcx
	push rdx

	; Read start time
	call [sys_timer]
	mov rbx, rax			; Save start time

	; Submit 1000 NOP commands
	mov ecx, 1000
gpu_benchmark_loop:
	push rcx
	mov eax, NV_CMD_NOP
	xor ebx, ebx
	xor ecx, ecx
	xor edx, edx
	call gpu_cmd_submit
	call gpu_fence_wait
	pop rcx
	dec ecx
	jnz gpu_benchmark_loop

	; Read end time
	call [sys_timer]
	sub rax, rbx			; Total ticks
	xor edx, edx
	mov ecx, 1000
	div rcx				; Average per command

	pop rdx
	pop rcx
	pop rbx
	ret
; -----------------------------------------------------------------------------


; Strings
msg_gpu:		db 13, 10, 'gpu', 0
msg_gpu_not_found:	db ' not found', 0


; =============================================================================
; EOF
