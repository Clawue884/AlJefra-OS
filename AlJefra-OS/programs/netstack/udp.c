// =============================================================================
// AlJefra OS — UDP Send/Receive
// =============================================================================

#include "netstack.h"

// DNS response callback (set by dns.c)
extern void dns_handle_response(net_state_t *ns, u8 *data, u32 len);

// Handle incoming UDP packet
void udp_handle(net_state_t *ns, u8 *pkt, u32 ip_hdr_len, u32 total_len) {
	ipv4_header_t *ip = (ipv4_header_t *)pkt;
	udp_header_t *udp = (udp_header_t *)(pkt + ip_hdr_len);
	u32 udp_len = total_len - ip_hdr_len;

	if (udp_len < sizeof(udp_header_t))
		return;

	u16 dst_port = ntohs(udp->dst_port);
	u16 src_port = ntohs(udp->src_port);
	u32 data_len = ntohs(udp->length) - sizeof(udp_header_t);
	u8 *data = (u8 *)udp + sizeof(udp_header_t);

	// Route to DNS handler if it's a DNS response (from port 53)
	if (src_port == 53) {
		dns_handle_response(ns, data, data_len);
	}
}

// Send a UDP packet
int udp_send(net_state_t *ns, u32 dst_ip, u16 src_port, u16 dst_port, u8 *data, u32 len) {
	u32 udp_total = sizeof(udp_header_t) + len;
	if (udp_total > ETH_MTU - sizeof(ipv4_header_t))
		return -1;

	u8 buf[ETH_MTU];
	udp_header_t *udp = (udp_header_t *)buf;

	udp->src_port = htons(src_port);
	udp->dst_port = htons(dst_port);
	udp->length = htons((u16)udp_total);
	udp->checksum = 0;

	net_memcpy(buf + sizeof(udp_header_t), data, len);

	// Calculate UDP checksum
	udp->checksum = udp_checksum(htonl(ns->local_ip), htonl(dst_ip), buf, udp_total);

	ipv4_send(ns, dst_ip, IP_PROTO_UDP, buf, udp_total);
	return 0;
}
