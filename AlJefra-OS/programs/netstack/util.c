// =============================================================================
// AlJefra OS — Utility functions (no libc)
// =============================================================================

#include "netstack.h"

void *net_memcpy(void *dst, const void *src, u64 n) {
	u8 *d = (u8 *)dst;
	const u8 *s = (const u8 *)src;
	while (n--)
		*d++ = *s++;
	return dst;
}

void *net_memset(void *dst, int c, u64 n) {
	u8 *d = (u8 *)dst;
	while (n--)
		*d++ = (u8)c;
	return dst;
}

void *net_memmove(void *dst, const void *src, u64 n) {
	u8 *d = (u8 *)dst;
	const u8 *s = (const u8 *)src;
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

int net_memcmp(const void *a, const void *b, u64 n) {
	const u8 *pa = (const u8 *)a;
	const u8 *pb = (const u8 *)b;
	for (u64 i = 0; i < n; i++) {
		if (pa[i] != pb[i])
			return pa[i] - pb[i];
	}
	return 0;
}

u32 net_strlen(const char *s) {
	u32 len = 0;
	while (*s++)
		len++;
	return len;
}

int net_strcmp(const char *a, const char *b) {
	while (*a && *a == *b) { a++; b++; }
	return (u8)*a - (u8)*b;
}

int net_strncmp(const char *a, const char *b, u64 n) {
	for (u64 i = 0; i < n; i++) {
		if (a[i] != b[i]) return (u8)a[i] - (u8)b[i];
		if (a[i] == 0) return 0;
	}
	return 0;
}

char *net_strcpy(char *dst, const char *src) {
	char *ret = dst;
	while ((*dst++ = *src++));
	return ret;
}

// Minimal sprintf supporting: %d, %u, %x, %s, %c, %%
// Returns number of chars written (excluding null terminator)
int mini_sprintf(char *buf, const char *fmt, ...) {
	__builtin_va_list ap;
	__builtin_va_start(ap, fmt);
	char *out = buf;

	while (*fmt) {
		if (*fmt != '%') {
			*out++ = *fmt++;
			continue;
		}
		fmt++;	// skip '%'

		// Check for '0' pad flag
		char pad = ' ';
		int width = 0;
		if (*fmt == '0') {
			pad = '0';
			fmt++;
		}
		// Parse width
		while (*fmt >= '0' && *fmt <= '9') {
			width = width * 10 + (*fmt - '0');
			fmt++;
		}
		// Handle 'l' modifier (ignored, all ints are promoted)
		if (*fmt == 'l') fmt++;

		switch (*fmt) {
		case 'd': {
			int val = __builtin_va_arg(ap, int);
			if (val < 0) { *out++ = '-'; val = -val; }
			// Convert digits to buffer (reverse)
			char tmp[12];
			int i = 0;
			if (val == 0) tmp[i++] = '0';
			while (val > 0) { tmp[i++] = '0' + (val % 10); val /= 10; }
			// Pad
			for (int j = i; j < width; j++) *out++ = pad;
			// Output reversed
			while (i > 0) *out++ = tmp[--i];
			break;
		}
		case 'u': {
			unsigned int val = __builtin_va_arg(ap, unsigned int);
			char tmp[12];
			int i = 0;
			if (val == 0) tmp[i++] = '0';
			while (val > 0) { tmp[i++] = '0' + (val % 10); val /= 10; }
			for (int j = i; j < width; j++) *out++ = pad;
			while (i > 0) *out++ = tmp[--i];
			break;
		}
		case 'x': {
			unsigned int val = __builtin_va_arg(ap, unsigned int);
			char tmp[9];
			int i = 0;
			if (val == 0) tmp[i++] = '0';
			while (val > 0) {
				int d = val & 0xf;
				tmp[i++] = d < 10 ? '0' + d : 'a' + d - 10;
				val >>= 4;
			}
			for (int j = i; j < width; j++) *out++ = pad;
			while (i > 0) *out++ = tmp[--i];
			break;
		}
		case 's': {
			const char *s = __builtin_va_arg(ap, const char *);
			if (!s) s = "(null)";
			while (*s) *out++ = *s++;
			break;
		}
		case 'c':
			*out++ = (char)__builtin_va_arg(ap, int);
			break;
		case '%':
			*out++ = '%';
			break;
		default:
			*out++ = '%';
			*out++ = *fmt;
			break;
		}
		fmt++;
	}
	*out = '\0';
	__builtin_va_end(ap);
	return (int)(out - buf);
}
