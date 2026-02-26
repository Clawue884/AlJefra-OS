// Diagnostic: add netstack calls one-by-one to find crash point
#include "netstack.h"

static void print(const char *s) {
	b_output(s, net_strlen(s));
}

int main(void) {
	print("DIAG: alive\n");

	// Step 1: Just call net_get_state
	print("DIAG: calling net_get_state...\n");
	net_state_t *ns = net_get_state();
	print("DIAG: net_get_state OK\n");

	// Step 2: Call net_init
	print("DIAG: calling net_init...\n");
	int r = net_init(IP4(10,0,0,2), IP4(10,0,0,1), IP4(255,255,255,0), IP4(8,8,8,8));
	if (r == 0)
		print("DIAG: net_init OK\n");
	else
		print("DIAG: net_init FAILED\n");

	print("DIAG: done\n");
	while (1) {
		unsigned char c = b_input();
		if (c == 'q') break;
	}
	return 0;
}
