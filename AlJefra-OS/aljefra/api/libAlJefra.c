// =============================================================================
// AlJefra OS -- a 64-bit OS written in Assembly for x86-64 systems
// Copyright (C) 2008-2026 AlJefra -- see LICENSE.TXT
//
// Version 1.0
// =============================================================================


#include "libAlJefra.h"


// Input/Output

u8 b_input(void) {
	u8 chr;
	asm volatile ("call *0x00100010" : "=a" (chr));
	return chr;
}

void b_output(const char *str, u64 nbr) {
	asm volatile ("call *0x00100018" : : "S"(str), "c"(nbr));
}


// Network

void b_net_tx(void *mem, u64 len, u64 iid) {
	asm volatile ("call *0x00100020" : : "S"(mem), "c"(len), "d"(iid));
}

u64 b_net_rx(void **mem, u64 iid) {
	u64 tlong;
	asm volatile ("call *0x00100028" : "=D"(*mem), "=c"(tlong) : "d"(iid));
	return tlong;
}


// Non-volatile Storage

u64 b_nvs_read(void *mem, u64 start, u64 num, u64 drivenum) {
	u64 tlong;
	asm volatile ("call *0x00100030" : "=c"(tlong) : "a"(start), "c"(num), "d"(drivenum), "D"(mem));
	return tlong;
}

u64 b_nvs_write(void *mem, u64 start, u64 num, u64 drivenum) {
	u64 tlong = 0;
	asm volatile ("call *0x00100038" : "=c"(tlong) : "a"(start), "c"(num), "d"(drivenum), "S"(mem));
	return tlong;
}


// System

u64 b_system(u64 function, u64 var1, u64 var2) {
	u64 tlong;
	asm volatile ("call *0x00100040" : "=a"(tlong) : "c"(function), "a"(var1), "d"(var2));
	return tlong;
}


// GPU Functions

u64 b_gpu_status(void) {
	u64 result;
	asm volatile ("call *0x00100050" : "=a" (result));
	return result;
}

u64 b_gpu_compute(void *params) {
	u64 result;
	asm volatile ("call *0x00100058" : "=a" (result) : "a"(params));
	return result;
}

u64 b_gpu_mem_alloc(u64 size) {
	u64 result;
	asm volatile ("call *0x00100060" : "=a" (result) : "c"(size));
	return result;
}

void b_gpu_mem_free(u64 offset, u64 size) {
	asm volatile ("call *0x00100068" : : "a"(offset), "c"(size));
}

u64 b_gpu_mem_copy_to(void *src, u64 dst, u64 size) {
	u64 result;
	asm volatile ("call *0x00100070" : "=a" (result) : "S"(src), "D"(dst), "c"(size));
	return result;
}

u64 b_gpu_mem_copy_from(u64 src, void *dst, u64 size) {
	u64 result;
	asm volatile ("call *0x00100078" : "=a" (result) : "S"(src), "D"(dst), "c"(size));
	return result;
}

void b_gpu_fence_wait(u64 fence) {
	asm volatile ("call *0x00100080" : : "a"(fence));
}

u32 b_gpu_mmio_read(u32 reg) {
	u32 result;
	asm volatile ("call *0x00100088" : "=a" (result) : "c"((u64)reg));
	return result;
}

void b_gpu_mmio_write(u32 reg, u32 val) {
	asm volatile ("call *0x00100090" : : "c"((u64)reg), "a"((u64)val));
}

u64 b_gpu_benchmark(void) {
	u64 result;
	asm volatile ("call *0x001000A0" : "=a" (result));
	return result;
}


// =============================================================================
// EOF
