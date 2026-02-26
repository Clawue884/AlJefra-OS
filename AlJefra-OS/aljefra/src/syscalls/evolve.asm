; =============================================================================
; AlJefra OS -- a 64-bit OS written in Assembly for x86-64 systems
; Copyright (C) 2008-2026 AlJefra -- see LICENSE.TXT
;
; Evolution Syscalls -- Self-benchmarking and evolution tracking
; =============================================================================


; -----------------------------------------------------------------------------
; b_evolve_benchmark -- Run a comprehensive system benchmark
;  IN:	RAX = Component ID (0-15, or 0xFF for all)
; OUT:	RAX = Benchmark score (higher = better)
;	RDX = Latency in timer ticks
b_evolve_benchmark:
	push rbx
	push rcx
	push rsi

	cmp al, 0xFF
	je b_evolve_benchmark_all

	; Single component benchmark
	cmp al, 0			; Kernel
	je b_evolve_bench_kernel
	cmp al, 2			; SMP
	je b_evolve_bench_smp
	cmp al, 5			; GPU
	je b_evolve_bench_gpu
	jmp b_evolve_bench_generic

b_evolve_benchmark_all:
	; Run all benchmarks, return aggregate score
	xor ebx, ebx			; Accumulator

	; Kernel benchmark
	call b_evolve_bench_kernel
	add rbx, rax

	; SMP benchmark
	call b_evolve_bench_smp
	add rbx, rax

	; GPU benchmark
	call b_evolve_bench_gpu
	add rbx, rax

	mov rax, rbx
	xor edx, edx
	pop rsi
	pop rcx
	pop rbx
	ret

b_evolve_bench_kernel:
	; Measure syscall dispatch latency
	call [sys_timer]
	mov rbx, rax			; Start time

	mov ecx, 10000
.loop:
	push rcx
	xor eax, eax
	xor ecx, ecx
	xor edx, edx
	; Call timecounter (lightweight syscall)
	call b_system_timecounter
	pop rcx
	dec ecx
	jnz .loop

	call [sys_timer]
	sub rax, rbx			; Total ticks
	mov rdx, rax			; Latency in RDX
	; Score = inverse of latency (scaled)
	mov rbx, 10000000000
	xor edx, edx
	div rbx
	mov rdx, rax			; Also store in RDX
	; Store benchmark result
	mov [os_Bench_KernelLatency], rax

	pop rsi
	pop rcx
	pop rbx
	ret

b_evolve_bench_smp:
	; Measure SMP core count and availability
	xor eax, eax
	mov ax, [os_NumCores]
	mov rdx, rax
	; Score = cores * 1000
	imul rax, 1000
	mov [os_Bench_SMPScaling], rax

	pop rsi
	pop rcx
	pop rbx
	ret

b_evolve_bench_gpu:
	; Measure GPU status and command latency
	cmp byte [os_GPUEnabled], 1
	jne b_evolve_bench_gpu_none

	call b_gpu_benchmark
	mov rdx, rax
	; Score = inverse of latency
	mov rbx, 1000000
	push rdx
	xor edx, edx
	div rbx
	pop rdx
	mov [os_Bench_GPULatency], rax

	pop rsi
	pop rcx
	pop rbx
	ret

b_evolve_bench_gpu_none:
	xor eax, eax
	xor edx, edx
	pop rsi
	pop rcx
	pop rbx
	ret

b_evolve_bench_generic:
	; Generic benchmark - measure timer read latency
	call [sys_timer]
	mov rbx, rax
	mov ecx, 1000
.gloop:
	call [sys_timer]
	dec ecx
	jnz .gloop
	sub rax, rbx
	mov rdx, rax

	pop rsi
	pop rcx
	pop rbx
	ret
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; b_evolve_status -- Get evolution engine status
;  IN:	Nothing
; OUT:	RAX = Status word:
;		Bits 0-7:   Current status (0=idle, 1=benchmarking, etc.)
;		Bits 8-15:  Active component ID
;		Bits 16-31: Total breakthroughs
;		Bits 32-63: Total generations
b_evolve_status:
	xor rax, rax
	mov al, [os_Evolve_Active]
	mov ah, [os_Evolve_Component]
	push rbx
	mov ebx, [os_Evolve_Breakthroughs]
	shl rbx, 16
	or rax, rbx
	mov rbx, [os_Evolve_Generation]
	shl rbx, 32
	or rax, rbx
	pop rbx
	ret
; -----------------------------------------------------------------------------


; -----------------------------------------------------------------------------
; b_evolve_get_benchmark -- Read stored benchmark results
;  IN:	AL = Benchmark ID:
;		0 = Kernel latency
;		1 = GPU latency
;		2 = Memory bandwidth
;		3 = Network latency
;		4 = Storage latency
;		5 = SMP scaling
; OUT:	RAX = Benchmark value
b_evolve_get_benchmark:
	cmp al, 0
	je .kernel
	cmp al, 1
	je .gpu
	cmp al, 2
	je .mem
	cmp al, 3
	je .net
	cmp al, 4
	je .nvs
	cmp al, 5
	je .smp
	xor eax, eax
	ret

.kernel:
	mov rax, [os_Bench_KernelLatency]
	ret
.gpu:
	mov rax, [os_Bench_GPULatency]
	ret
.mem:
	mov rax, [os_Bench_MemBandwidth]
	ret
.net:
	mov rax, [os_Bench_NetLatency]
	ret
.nvs:
	mov rax, [os_Bench_NVSLatency]
	ret
.smp:
	mov rax, [os_Bench_SMPScaling]
	ret
; -----------------------------------------------------------------------------


; =============================================================================
; EOF
