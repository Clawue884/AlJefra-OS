// =============================================================================
// AlJefra OS — Freestanding libc shims for BearSSL
//
// BearSSL needs memcpy, memset, memmove, memcmp from <string.h>.
// We provide them here since we're running on bare metal with no libc.
// =============================================================================

#include <stddef.h>

void *memcpy(void *dst, const void *src, size_t n) {
	unsigned char *d = (unsigned char *)dst;
	const unsigned char *s = (const unsigned char *)src;
	while (n--)
		*d++ = *s++;
	return dst;
}

void *memset(void *dst, int c, size_t n) {
	unsigned char *d = (unsigned char *)dst;
	while (n--)
		*d++ = (unsigned char)c;
	return dst;
}

void *memmove(void *dst, const void *src, size_t n) {
	unsigned char *d = (unsigned char *)dst;
	const unsigned char *s = (const unsigned char *)src;
	if (d < s) {
		while (n--)
			*d++ = *s++;
	} else {
		d += n;
		s += n;
		while (n--)
			*--d = *--s;
	}
	return dst;
}

int memcmp(const void *a, const void *b, size_t n) {
	const unsigned char *pa = (const unsigned char *)a;
	const unsigned char *pb = (const unsigned char *)b;
	for (size_t i = 0; i < n; i++) {
		if (pa[i] != pb[i])
			return pa[i] - pb[i];
	}
	return 0;
}

size_t strlen(const char *s) {
	size_t len = 0;
	while (*s++)
		len++;
	return len;
}

// BearSSL's settings.c references abort() — provide a stub
void abort(void) {
	// Halt the CPU
	while (1) {
		asm volatile ("hlt");
	}
}
