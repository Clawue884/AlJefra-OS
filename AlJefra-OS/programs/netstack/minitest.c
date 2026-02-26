// Progressive test to find the crash point
#include "netstack.h"

static void print(const char *s) {
	b_output(s, net_strlen(s));
}

static void println(const char *s) {
	print(s);
	print("\n");
}

int main(void) {
	println("=== AlJefra Net Stack Test ===");
	println("");

	// Step 1: init network
	print("Step 1: net_init... ");
	if (net_init(IP4(10,0,0,2), IP4(10,0,0,1), IP4(255,255,255,0), IP4(8,8,8,8)) != 0) {
		println("FAILED (no NIC)");
		return -1;
	}
	println("OK");

	// Step 2: show MAC
	net_state_t *ns = net_get_state();
	char buf[128];
	mini_sprintf(buf, "MAC: %02x:%02x:%02x:%02x:%02x:%02x",
		ns->local_mac[0], ns->local_mac[1], ns->local_mac[2],
		ns->local_mac[3], ns->local_mac[4], ns->local_mac[5]);
	println(buf);

	// Step 3: ARP resolve
	print("Step 2: ARP resolve 10.0.0.1... ");
	u8 gw_mac[6];
	int tries = 0;
	while (arp_resolve(ns, IP4(10,0,0,1), gw_mac) != 0) {
		for (int i = 0; i < 200; i++)
			net_poll();
		tries++;
		if (tries > 50) {
			println("TIMEOUT");
			goto test_ping;
		}
	}
	mini_sprintf(buf, "OK (%02x:%02x:%02x:%02x:%02x:%02x)",
		gw_mac[0], gw_mac[1], gw_mac[2],
		gw_mac[3], gw_mac[4], gw_mac[5]);
	println(buf);

test_ping:
	// Step 4: ICMP ping
	print("Step 3: ICMP ping 10.0.0.1... ");
	if (icmp_ping(IP4(10,0,0,1), 3000) == 0)
		println("OK");
	else
		println("No response");

	// Step 5: TCP connect (host should run: nc -l 7777)
	print("Step 4: TCP connect 10.0.0.1:7777... ");
	tcp_conn_t *conn = tcp_connect(IP4(10,0,0,1), 7777);
	if (!conn) {
		println("FAILED");
		goto done;
	}
	println("ESTABLISHED");

	// Step 6: TCP send
	print("Step 5: TCP send... ");
	const char *msg = "HELLO FROM ALJEFRA\r\n";
	int sent = tcp_send(conn, msg, net_strlen(msg));
	mini_sprintf(buf, "OK (%d bytes)", sent);
	println(buf);

	// Step 7: TCP close
	tcp_close(conn);
	println("Step 6: TCP close OK");

done:
	println("");
	println("=== Test Complete ===");

	while (1) {
		net_poll();
		u8 c = b_input();
		if (c == 'q') break;
	}
	return 0;
}
