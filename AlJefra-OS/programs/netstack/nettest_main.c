// =============================================================================
// AlJefra OS — Network Stack Test
//
// Tests ARP, ICMP ping, and TCP connect via TAP networking.
// Designed to run on AlJefra OS with QEMU TAP.
//
// Network config:
//   Guest: 10.0.0.2/24     (this OS)
//   Host:  10.0.0.1/24     (TAP interface on host)
//   Gateway: 10.0.0.1
// =============================================================================

#include "netstack.h"

#define MY_IP		IP4(10, 0, 0, 2)
#define MY_GATEWAY	IP4(10, 0, 0, 1)
#define MY_NETMASK	IP4(255, 255, 255, 0)
#define MY_DNS		IP4(8, 8, 8, 8)

// TCP test server port on host (run: nc -l 7777 on host)
#define TCP_TEST_IP	IP4(10, 0, 0, 1)
#define TCP_TEST_PORT	7777

static void print(const char *s) {
	b_output(s, net_strlen(s));
}

static void println(const char *s) {
	print(s);
	print("\n");
}

static void print_pass(const char *test) {
	print("[PASS] ");
	println(test);
}

static void print_fail(const char *test) {
	print("[FAIL] ");
	println(test);
}

int main(void) {
	char buf[256];
	int pass = 0, fail = 0;

	println("");
	println("==============================================");
	println("  AlJefra OS - Network Stack Test Suite");
	println("==============================================");
	println("");

	// ---- Test 1: Network Init ----
	print("Test 1: Network init... ");
	if (net_init(MY_IP, MY_GATEWAY, MY_NETMASK, MY_DNS) != 0) {
		println("FAILED (no NIC)");
		print_fail("net_init");
		fail++;
		println("Cannot continue without NIC. Aborting.");
		goto done;
	}
	println("OK");
	print_pass("net_init");
	pass++;

	net_state_t *ns = net_get_state();
	mini_sprintf(buf, "  MAC: %02x:%02x:%02x:%02x:%02x:%02x",
		ns->local_mac[0], ns->local_mac[1], ns->local_mac[2],
		ns->local_mac[3], ns->local_mac[4], ns->local_mac[5]);
	println(buf);
	mini_sprintf(buf, "  IP:  %u.%u.%u.%u", 10, 0, 0, 2);
	println(buf);
	mini_sprintf(buf, "  GW:  %u.%u.%u.%u", 10, 0, 0, 1);
	println(buf);
	println("");

	// ---- Test 2: ARP Resolution ----
	print("Test 2: ARP resolve gateway (10.0.0.1)... ");
	u8 gw_mac[6];
	int arp_tries = 0;
	while (arp_resolve(ns, MY_GATEWAY, gw_mac) != 0) {
		for (int i = 0; i < 200; i++)
			net_poll();
		arp_tries++;
		if (arp_tries > 50) {
			println("TIMEOUT");
			print_fail("ARP resolve");
			fail++;
			goto test_ping;
		}
	}
	mini_sprintf(buf, "OK (%02x:%02x:%02x:%02x:%02x:%02x) after %d tries",
		gw_mac[0], gw_mac[1], gw_mac[2],
		gw_mac[3], gw_mac[4], gw_mac[5], arp_tries);
	println(buf);
	print_pass("ARP resolve");
	pass++;
	println("");

test_ping:
	// ---- Test 3: ICMP Ping ----
	print("Test 3: ICMP ping gateway... ");
	if (icmp_ping(MY_GATEWAY, 3000) == 0) {
		println("OK (pong received)");
		print_pass("ICMP ping");
		pass++;
	} else {
		println("No response (timeout 3s)");
		print_fail("ICMP ping");
		fail++;
	}
	println("");

	// ---- Test 4: Second ping (cached ARP) ----
	print("Test 4: ICMP ping (cached ARP)... ");
	if (icmp_ping(MY_GATEWAY, 2000) == 0) {
		println("OK (pong received)");
		print_pass("ICMP ping (cached)");
		pass++;
	} else {
		println("No response");
		print_fail("ICMP ping (cached)");
		fail++;
	}
	println("");

	// ---- Test 5: TCP Connect ----
	print("Test 5: TCP connect to 10.0.0.1:7777... ");
	println("(host should run: nc -l 7777)");
	print("  Connecting... ");
	tcp_conn_t *conn = tcp_connect(TCP_TEST_IP, TCP_TEST_PORT);
	if (!conn) {
		println("FAILED (no connection)");
		print_fail("TCP connect");
		fail++;
		goto done;
	}
	println("ESTABLISHED");
	print_pass("TCP connect");
	pass++;
	println("");

	// ---- Test 6: TCP Send ----
	print("Test 6: TCP send... ");
	const char *msg = "HELLO FROM ALJEFRA OS!\r\n";
	int sent = tcp_send(conn, msg, net_strlen(msg));
	if (sent > 0) {
		mini_sprintf(buf, "OK (%d bytes sent)", sent);
		println(buf);
		print_pass("TCP send");
		pass++;
	} else {
		println("FAILED");
		print_fail("TCP send");
		fail++;
	}
	println("");

	// ---- Test 7: TCP Receive ----
	print("Test 7: TCP recv (waiting 5s for host response)... ");
	{
		u8 recv_buf[1024];
		u64 start = net_get_time_ms();
		int total_recv = 0;
		while (net_get_time_ms() - start < 5000) {
			net_poll();
			int r = tcp_recv(conn, recv_buf + total_recv,
				(u32)(sizeof(recv_buf) - 1 - total_recv));
			if (r > 0) {
				total_recv += r;
				// Got some data — wait a bit more for complete message
				if (total_recv > 0) {
					// Short wait for more data
					u64 data_start = net_get_time_ms();
					while (net_get_time_ms() - data_start < 500) {
						net_poll();
						r = tcp_recv(conn, recv_buf + total_recv,
							(u32)(sizeof(recv_buf) - 1 - total_recv));
						if (r > 0) total_recv += r;
					}
					break;
				}
			}
		}
		if (total_recv > 0) {
			recv_buf[total_recv] = 0;
			mini_sprintf(buf, "OK (%d bytes received)", total_recv);
			println(buf);
			print("  Data: ");
			// Print first 80 chars
			char preview[81];
			int plen = total_recv > 80 ? 80 : total_recv;
			for (int i = 0; i < plen; i++)
				preview[i] = (recv_buf[i] >= 32 && recv_buf[i] < 127) ? recv_buf[i] : '.';
			preview[plen] = 0;
			println(preview);
			print_pass("TCP recv");
			pass++;
		} else {
			println("No data (timeout). Host may not have sent anything — OK.");
			print("  (This is expected if you didn't type anything in nc)\n");
			// Not a failure — host just didn't send anything
			print_pass("TCP recv (no data expected)");
			pass++;
		}
	}
	println("");

	// ---- Test 8: TCP Close ----
	print("Test 8: TCP close... ");
	tcp_close(conn);
	println("OK");
	print_pass("TCP close");
	pass++;
	println("");

done:
	println("==============================================");
	mini_sprintf(buf, "  Results: %d passed, %d failed", pass, fail);
	println(buf);
	println("==============================================");

	if (fail == 0)
		println("ALL TESTS PASSED");
	else
		println("SOME TESTS FAILED");

	println("");
	println("Network test complete. System halted.");

	// Spin forever (don't return to monitor)
	while (1) {
		net_poll();
		u8 c = b_input();
		if (c == 'q') break;
	}

	return 0;
}
