// =============================================================================
// AlJefra OS — ICMP (Echo Request/Reply)
// =============================================================================

#include "netstack.h"

#define ICMP_ECHO_REPLY		0
#define ICMP_ECHO_REQUEST	8

// Pending ping state
static volatile u8 ping_received;
static u16 ping_id;
static u16 ping_seq;

// Handle incoming ICMP packet
void icmp_handle(net_state_t *ns, u8 *pkt, u32 ip_hdr_len, u32 total_len) {
	ipv4_header_t *ip = (ipv4_header_t *)pkt;
	icmp_header_t *icmp = (icmp_header_t *)(pkt + ip_hdr_len);
	u32 icmp_len = total_len - ip_hdr_len;

	if (icmp_len < sizeof(icmp_header_t))
		return;

	if (icmp->type == ICMP_ECHO_REQUEST && icmp->code == 0) {
		// Reply to ping
		u32 payload_len = icmp_len;
		u8 reply[ETH_MTU];

		net_memcpy(reply, icmp, payload_len);
		icmp_header_t *rep = (icmp_header_t *)reply;
		rep->type = ICMP_ECHO_REPLY;
		rep->checksum = 0;
		rep->checksum = ip_checksum(rep, payload_len);

		ipv4_send(ns, ntohl(ip->src_ip), IP_PROTO_ICMP, reply, payload_len);
	}
	else if (icmp->type == ICMP_ECHO_REPLY && icmp->code == 0) {
		// Check if this matches our outstanding ping
		if (ntohs(icmp->id) == ping_id && ntohs(icmp->seq) == ping_seq)
			ping_received = 1;
	}
}

// Send ICMP echo request and wait for reply
// Returns 0 on success (pong received), -1 on timeout
int icmp_ping(u32 ip, u32 timeout_ms) {
	net_state_t *ns = net_get_state();

	ping_received = 0;
	ping_id = 0x1234;
	ping_seq++;

	// Build ICMP echo request
	u8 buf[64];
	icmp_header_t *icmp = (icmp_header_t *)buf;
	icmp->type = ICMP_ECHO_REQUEST;
	icmp->code = 0;
	icmp->checksum = 0;
	icmp->id = htons(ping_id);
	icmp->seq = htons(ping_seq);

	// Fill payload with pattern
	for (int i = sizeof(icmp_header_t); i < 64; i++)
		buf[i] = (u8)i;

	icmp->checksum = ip_checksum(buf, 64);

	// Send
	ipv4_send(ns, ip, IP_PROTO_ICMP, buf, 64);

	// Wait for reply
	u64 start = net_get_time_ms();
	while (!ping_received) {
		net_poll();
		if (net_get_time_ms() - start > timeout_ms)
			return -1;
	}
	return 0;
}
