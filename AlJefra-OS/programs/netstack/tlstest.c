// =============================================================================
// AlJefra OS — TLS Handshake Test
// Tests: DNS resolve → TCP connect → TLS handshake → HTTPS GET → response
// Target: example.com:443 (uses ISRG Root X1 / DigiCert chain)
// =============================================================================

#include "netstack.h"
#include "tls.h"

static void print(const char *s) {
	b_output(s, net_strlen(s));
}

static void println(const char *s) {
	print(s);
	print("\n");
}

static void print_ip(u32 ip) {
	char buf[20];
	mini_sprintf(buf, "%d.%d.%d.%d",
		(ip >> 24) & 0xFF, (ip >> 16) & 0xFF,
		(ip >> 8) & 0xFF, ip & 0xFF);
	print(buf);
}

int main(void) {
	char buf[256];

	println("=== AlJefra TLS Handshake Test ===");
	println("");

	// Step 1: Init network
	print("Step 1: net_init... ");
	if (net_init(IP4(10,0,0,2), IP4(10,0,0,1), IP4(255,255,255,0), IP4(8,8,8,8)) != 0) {
		println("FAILED");
		goto halt;
	}
	println("OK");

	// Step 2: Init TLS
	print("Step 2: tls_init... ");
	tls_init();
	println("OK");

	// Step 3: DNS resolve
	print("Step 3: DNS resolve example.com... ");
	u32 target_ip = dns_resolve("example.com");
	if (target_ip == 0) {
		println("FAILED (using hardcoded IP)");
		// example.com = 93.184.215.14 (as of 2025)
		target_ip = IP4(93,184,215,14);
	}
	print("OK (");
	print_ip(target_ip);
	println(")");

	// Step 4: TLS connect
	print("Step 4: TLS connect example.com:443... ");
	tls_conn_t *tls = tls_connect(target_ip, 443, "example.com");
	if (!tls) {
		println("FAILED");
		// Try to get BearSSL error info
		println("  (TLS handshake or TCP connection failed)");
		goto halt;
	}
	println("OK");

	// Step 5: Send HTTP GET
	print("Step 5: HTTP GET /... ");
	const char *request =
		"GET / HTTP/1.1\r\n"
		"Host: example.com\r\n"
		"Connection: close\r\n"
		"\r\n";
	int sent = tls_send(tls, request, net_strlen(request));
	if (sent < 0) {
		int err = tls_get_error(tls);
		mini_sprintf(buf, "FAILED (err=%d)", err);
		println(buf);
		goto cleanup;
	}
	mini_sprintf(buf, "OK (%d bytes)", sent);
	println(buf);

	// Step 6: Read response
	print("Step 6: HTTP response... ");
	char resp[2048];
	int total_recv = 0;
	int n;
	while (total_recv < (int)sizeof(resp) - 1) {
		n = tls_recv(tls, resp + total_recv, sizeof(resp) - 1 - total_recv);
		if (n <= 0) break;
		total_recv += n;
	}
	resp[total_recv] = '\0';

	if (total_recv > 0) {
		mini_sprintf(buf, "OK (%d bytes)", total_recv);
		println(buf);

		// Show first line of response (HTTP status)
		print("  Response: ");
		// Find first \r\n
		int end = 0;
		while (end < total_recv && resp[end] != '\r' && resp[end] != '\n' && end < 80)
			end++;
		resp[end] = '\0';
		println(resp);
	} else {
		println("FAILED (no data)");
	}

cleanup:
	// Step 7: Close
	print("Step 7: TLS close... ");
	tls_close(tls);
	println("OK");

halt:
	println("");
	println("=== TLS Test Complete ===");

	while (1) {
		net_poll();
		u8 c = b_input();
		if (c == 'q') break;
	}
	return 0;
}
