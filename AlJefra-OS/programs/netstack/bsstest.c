// Test: large BSS with no netstack
// If this crashes, BSS overlaps with stack

#include "../../aljefra/api/libAlJefra.h"

// Force a large BSS section (~132KB, same as netstack)
static char big_bss[135000];

static unsigned long my_strlen(const char *s) {
	unsigned long n = 0;
	while (s[n]) n++;
	return n;
}

static void print(const char *s) {
	b_output(s, my_strlen(s));
}

int main(void) {
	print("BSS test: alive!\n");

	// Touch BSS to prove it's zeroed
	int ok = 1;
	for (int i = 0; i < 135000; i++) {
		if (big_bss[i] != 0) {
			ok = 0;
			break;
		}
	}

	if (ok)
		print("BSS zeroed OK\n");
	else
		print("BSS NOT zeroed!\n");

	// Write something to BSS
	big_bss[0] = 'A';
	big_bss[134999] = 'Z';
	print("BSS write OK\n");

	print("Test PASSED\n");

	while (1) {
		unsigned char c = b_input();
		if (c == 'q') break;
	}
	return 0;
}
