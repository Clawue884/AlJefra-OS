// =============================================================================
// AlJefra OS — ARP (Address Resolution Protocol)
// =============================================================================

#include "netstack.h"

#define ARP_HW_ETHERNET	1
#define ARP_OP_REQUEST	1
#define ARP_OP_REPLY	2

// Handle incoming ARP packet
void arp_handle(net_state_t *ns, u8 *pkt, u32 len) {
	if (len < sizeof(arp_packet_t))
		return;

	arp_packet_t *arp = (arp_packet_t *)pkt;

	// Only handle Ethernet/IPv4
	if (ntohs(arp->hw_type) != ARP_HW_ETHERNET)
		return;
	if (ntohs(arp->proto_type) != ETH_TYPE_IP)
		return;

	u32 sender_ip = ntohl(arp->sender_ip);
	u32 target_ip = ntohl(arp->target_ip);

	// Update ARP cache with sender info
	for (int i = 0; i < ARP_CACHE_SIZE; i++) {
		if (ns->arp_cache[i].ip == sender_ip) {
			net_memcpy(ns->arp_cache[i].mac, arp->sender_mac, ETH_ALEN);
			ns->arp_cache[i].valid = 1;
			break;
		}
	}
	// If not found, add to first empty or overwrite oldest
	for (int i = 0; i < ARP_CACHE_SIZE; i++) {
		if (!ns->arp_cache[i].valid) {
			ns->arp_cache[i].ip = sender_ip;
			net_memcpy(ns->arp_cache[i].mac, arp->sender_mac, ETH_ALEN);
			ns->arp_cache[i].valid = 1;
			break;
		}
	}

	// Handle ARP request for our IP
	if (ntohs(arp->opcode) == ARP_OP_REQUEST && target_ip == ns->local_ip) {
		// Build ARP reply
		u8 *frame = ns->tx_frame;
		eth_header_t *eth = (eth_header_t *)frame;
		arp_packet_t *reply = (arp_packet_t *)(frame + ETH_HLEN);

		// Ethernet header
		net_memcpy(eth->dst, arp->sender_mac, ETH_ALEN);
		net_memcpy(eth->src, ns->local_mac, ETH_ALEN);
		eth->type = htons(ETH_TYPE_ARP);

		// ARP reply
		reply->hw_type = htons(ARP_HW_ETHERNET);
		reply->proto_type = htons(ETH_TYPE_IP);
		reply->hw_len = ETH_ALEN;
		reply->proto_len = 4;
		reply->opcode = htons(ARP_OP_REPLY);
		net_memcpy(reply->sender_mac, ns->local_mac, ETH_ALEN);
		reply->sender_ip = htonl(ns->local_ip);
		net_memcpy(reply->target_mac, arp->sender_mac, ETH_ALEN);
		reply->target_ip = arp->sender_ip;

		b_net_tx(frame, ETH_HLEN + sizeof(arp_packet_t), ns->iid);
	}
}

// Send an ARP request for the given IP
void arp_request(net_state_t *ns, u32 target_ip) {
	u8 *frame = ns->tx_frame;
	eth_header_t *eth = (eth_header_t *)frame;
	arp_packet_t *arp = (arp_packet_t *)(frame + ETH_HLEN);

	// Ethernet header — broadcast
	net_memset(eth->dst, 0xFF, ETH_ALEN);
	net_memcpy(eth->src, ns->local_mac, ETH_ALEN);
	eth->type = htons(ETH_TYPE_ARP);

	// ARP request
	arp->hw_type = htons(ARP_HW_ETHERNET);
	arp->proto_type = htons(ETH_TYPE_IP);
	arp->hw_len = ETH_ALEN;
	arp->proto_len = 4;
	arp->opcode = htons(ARP_OP_REQUEST);
	net_memcpy(arp->sender_mac, ns->local_mac, ETH_ALEN);
	arp->sender_ip = htonl(ns->local_ip);
	net_memset(arp->target_mac, 0x00, ETH_ALEN);
	arp->target_ip = htonl(target_ip);

	b_net_tx(frame, ETH_HLEN + sizeof(arp_packet_t), ns->iid);
}

// Resolve IP to MAC address. Returns 0 on success, -1 if not cached.
// Sends ARP request if not in cache.
int arp_resolve(net_state_t *ns, u32 ip, u8 *mac_out) {
	// If destination is on different subnet, use gateway MAC
	if ((ip & ns->netmask) != (ns->local_ip & ns->netmask))
		ip = ns->gateway;

	// Broadcast address
	if (ip == 0xFFFFFFFF) {
		net_memset(mac_out, 0xFF, ETH_ALEN);
		return 0;
	}

	// Search ARP cache
	for (int i = 0; i < ARP_CACHE_SIZE; i++) {
		if (ns->arp_cache[i].valid && ns->arp_cache[i].ip == ip) {
			net_memcpy(mac_out, ns->arp_cache[i].mac, ETH_ALEN);
			return 0;
		}
	}

	// Not in cache — send ARP request
	arp_request(ns, ip);
	return -1;
}
