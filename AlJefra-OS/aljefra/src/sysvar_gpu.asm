; =============================================================================
; AlJefra OS -- a 64-bit OS written in Assembly for x86-64 systems
; Copyright (C) 2008-2026 AlJefra -- see LICENSE.TXT
;
; GPU System Variables
; Extends sysvar.asm with GPU-specific system variables
; =============================================================================


; GPU System Variables - DQ (8 bytes each)
; Starting at offset 0x2000 in SystemVariables (after existing vars)
os_GPU_BAR0:		equ os_SystemVariables + 0x2000	; GPU MMIO base address (BAR0)
os_GPU_BAR1:		equ os_SystemVariables + 0x2008	; GPU VRAM aperture base (BAR1)
os_GPU_BAR0_Size:	equ os_SystemVariables + 0x2010	; BAR0 size in bytes
os_GPU_BAR1_Size:	equ os_SystemVariables + 0x2018	; BAR1 size in bytes
os_GPU_CmdQ_Base:	equ os_SystemVariables + 0x2020	; Command queue base address
os_GPU_TimerBase:	equ os_SystemVariables + 0x2028	; GPU timer baseline

; GPU System Variables - DD (4 bytes each)
os_GPU_BusAddr:		equ os_SystemVariables + 0x2080	; PCIe bus address of GPU
os_GPU_ChipID:		equ os_SystemVariables + 0x2084	; GPU chip identification
os_GPU_VRAM_MiB:	equ os_SystemVariables + 0x2088	; Total VRAM in MiB
os_GPU_VRAM_Pages:	equ os_SystemVariables + 0x208C	; Total VRAM pages (2MB each)
os_GPU_CmdQ_Head:	equ os_SystemVariables + 0x2090	; Command queue head pointer
os_GPU_CmdQ_Tail:	equ os_SystemVariables + 0x2094	; Command queue tail pointer
os_GPU_FenceCounter:	equ os_SystemVariables + 0x2098	; Monotonic fence counter
os_GPU_FenceCompleted:	equ os_SystemVariables + 0x209C	; Last completed fence ID

; GPU System Variables - DW (2 bytes each)
os_GPU_DeviceID:	equ os_SystemVariables + 0x20C0	; PCI Device ID

; GPU System Variables - DB (1 byte each)
os_GPUEnabled:		equ os_SystemVariables + 0x20E0	; 1 if GPU is initialized
os_GPU_ComputeReady:	equ os_SystemVariables + 0x20E1	; 1 if compute engine is ready

; GPU VRAM Bitmap (1 bit per 2MB page, supports up to 32GB = 2048 bytes)
os_GPU_VRAM_Bitmap:	equ os_SystemVariables + 0x3000	; 0x3000 -> 0x37FF (2KB)

; HPET tick counter (incremented by timer interrupt for preemptive scheduling)
os_hpet_ticks:		equ os_SystemVariables + 0x3800

; Evolution Engine Variables - DQ
os_Evolve_Generation:	equ os_SystemVariables + 0x4000	; Current evolution generation
os_Evolve_BestFitness:	equ os_SystemVariables + 0x4008	; Best fitness score (float64)
os_Evolve_StartTime:	equ os_SystemVariables + 0x4010	; Evolution start timestamp
os_Evolve_Breakthroughs: equ os_SystemVariables + 0x4018 ; Number of breakthroughs
os_Evolve_PopBase:	equ os_SystemVariables + 0x4020	; Population buffer base

; Evolution Engine Variables - DD
os_Evolve_PopSize:	equ os_SystemVariables + 0x4080	; Population size
os_Evolve_GenomeSize:	equ os_SystemVariables + 0x4084	; Genome size in bytes
os_Evolve_MutRate:	equ os_SystemVariables + 0x4088	; Mutation rate (float32)

; Evolution Engine Variables - DB
os_Evolve_Active:	equ os_SystemVariables + 0x40C0	; 1 if evolution is running
os_Evolve_Component:	equ os_SystemVariables + 0x40C1	; Current component being evolved

; Benchmark storage
os_Bench_KernelLatency:	equ os_SystemVariables + 0x5000	; Kernel syscall latency (ticks)
os_Bench_GPULatency:	equ os_SystemVariables + 0x5008	; GPU command latency (ticks)
os_Bench_MemBandwidth:	equ os_SystemVariables + 0x5010	; Memory bandwidth (bytes/tick)
os_Bench_NetLatency:	equ os_SystemVariables + 0x5018	; Network latency (ticks)
os_Bench_NVSLatency:	equ os_SystemVariables + 0x5020	; Storage latency (ticks)
os_Bench_SMPScaling:	equ os_SystemVariables + 0x5028	; SMP scaling efficiency (0-100)
os_Bench_Timestamp:	equ os_SystemVariables + 0x5030	; When benchmark was last run


; =============================================================================
; EOF
