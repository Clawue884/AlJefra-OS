// =============================================================================
// AlJefra OS — IPv4 Send/Receive
// =============================================================================

#include "netstack.h"

// Handle incoming IPv4 packet (after Ethernet header is stripped)
void ipv4_handle(net_state_t *ns, u8 *pkt, u32 len) {
	if (len < sizeof(ipv4_header_t))
		return;

	ipv4_header_t *ip = (ipv4_header_t *)pkt;

	// Check version
	if ((ip->ver_ihl >> 4) != 4)
		return;

	u32 ip_hdr_len = (ip->ver_ihl & 0x0F) * 4;
	u32 total_len = ntohs(ip->total_len);

	if (ip_hdr_len < 20 || total_len > len)
		return;

	// Verify checksum
	u16 saved_cksum = ip->checksum;
	ip->checksum = 0;
	u16 calc_cksum = ip_checksum(ip, ip_hdr_len);
	ip->checksum = saved_cksum;
	if (calc_cksum != saved_cksum)
		return;

	// Check destination — accept if it's our IP or broadcast
	u32 dst = ntohl(ip->dst_ip);
	if (dst != ns->local_ip && dst != 0xFFFFFFFF &&
	    dst != (ns->local_ip | ~ns->netmask))
		return;

	// Dispatch by protocol
	u8 *payload = pkt + ip_hdr_len;
	switch (ip->protocol) {
	case IP_PROTO_ICMP:
		icmp_handle(ns, pkt, ip_hdr_len, total_len);
		break;
	case IP_PROTO_TCP:
		tcp_handle(ns, pkt, ip_hdr_len, total_len);
		break;
	case IP_PROTO_UDP:
		udp_handle(ns, pkt, ip_hdr_len, total_len);
		break;
	}
}

// Send an IPv4 packet
void ipv4_send(net_state_t *ns, u32 dst_ip, u8 proto, u8 *payload, u32 payload_len) {
	u8 *frame = ns->tx_frame;
	eth_header_t *eth = (eth_header_t *)frame;
	ipv4_header_t *ip = (ipv4_header_t *)(frame + ETH_HLEN);

	u32 ip_total = sizeof(ipv4_header_t) + payload_len;
	if (ip_total > ETH_MTU)
		return;		// No fragmentation support

	// Resolve destination MAC
	u8 dst_mac[ETH_ALEN];
	if (arp_resolve(ns, dst_ip, dst_mac) != 0) {
		// ARP not resolved yet — packet dropped, caller must retry
		return;
	}

	// Ethernet header
	net_memcpy(eth->dst, dst_mac, ETH_ALEN);
	net_memcpy(eth->src, ns->local_mac, ETH_ALEN);
	eth->type = htons(ETH_TYPE_IP);

	// IPv4 header
	ip->ver_ihl = 0x45;		// Version 4, IHL = 5 (20 bytes)
	ip->tos = 0;
	ip->total_len = htons((u16)ip_total);
	ip->id = htons(ns->ip_id++);
	ip->flags_frag = htons(0x4000);	// Don't Fragment
	ip->ttl = 64;
	ip->protocol = proto;
	ip->checksum = 0;
	ip->src_ip = htonl(ns->local_ip);
	ip->dst_ip = htonl(dst_ip);

	// Calculate IP header checksum
	ip->checksum = ip_checksum(ip, sizeof(ipv4_header_t));

	// Copy payload after IP header
	net_memcpy(frame + ETH_HLEN + sizeof(ipv4_header_t), payload, payload_len);

	// Transmit
	b_net_tx(frame, ETH_HLEN + ip_total, ns->iid);
}
