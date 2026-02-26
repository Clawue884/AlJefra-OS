// =============================================================================
// AlJefra OS — Network Stack Init + Poll Loop
// =============================================================================

#include "netstack.h"

// Global network state (single instance)
static net_state_t g_net_state;

net_state_t *net_get_state(void) {
	return &g_net_state;
}

// Get millisecond time from TIMECOUNTER
// AlJefra OS TIMECOUNTER returns nanoseconds; divide by 1,000,000 to get ms
u64 net_get_time_ms(void) {
	return b_system(TIMECOUNTER, 0, 0) / 1000000;
}

// Initialize the network stack
// ip, gateway, netmask, dns are all in host byte order (e.g., IP4(10,0,0,2))
int net_init(u32 ip, u32 gateway, u32 netmask, u32 dns) {
	net_state_t *ns = &g_net_state;
	net_memset(ns, 0, sizeof(net_state_t));

	ns->local_ip = ip;
	ns->gateway = gateway;
	ns->netmask = netmask;
	ns->dns_server = dns;
	// Randomize ephemeral port start using TSC to avoid stale host state
	u64 tsc = b_system(TSC, 0, 0);
	ns->next_port = 49152 + (u16)(tsc & 0x3FFF);	// 49152-65535
	ns->ip_id = 1;
	ns->iid = 0;			// Use first NIC

	// Get MAC address from kernel
	u64 mac_raw = b_system(NET_STATUS, 0, 0);
	if (mac_raw == 0)
		return -1;		// No NIC

	// MAC is in lower 48 bits of mac_raw
	ns->local_mac[0] = (mac_raw >> 0) & 0xFF;
	ns->local_mac[1] = (mac_raw >> 8) & 0xFF;
	ns->local_mac[2] = (mac_raw >> 16) & 0xFF;
	ns->local_mac[3] = (mac_raw >> 24) & 0xFF;
	ns->local_mac[4] = (mac_raw >> 32) & 0xFF;
	ns->local_mac[5] = (mac_raw >> 40) & 0xFF;

	return 0;
}

// Poll for incoming packets and process them
void net_poll(void) {
	net_state_t *ns = &g_net_state;
	void *pkt_ptr = 0;
	u64 pkt_len;

	pkt_len = b_net_rx(&pkt_ptr, ns->iid);
	if (pkt_len == 0 || !pkt_ptr)
		return;

	// Copy packet to our buffer (kernel DMA buffer may be reused on next call)
	if (pkt_len > ETH_FRAME_MAX) pkt_len = ETH_FRAME_MAX;
	net_memcpy(ns->rx_copy, pkt_ptr, pkt_len);
	u8 *frame = ns->rx_copy;

	if (pkt_len < ETH_HLEN)
		return;

	eth_header_t *eth = (eth_header_t *)frame;
	u16 ethertype = ntohs(eth->type);
	u8 *payload = frame + ETH_HLEN;
	u32 payload_len = (u32)pkt_len - ETH_HLEN;

	switch (ethertype) {
	case ETH_TYPE_ARP:
		arp_handle(ns, payload, payload_len);
		break;
	case ETH_TYPE_IP:
		ipv4_handle(ns, payload, payload_len);
		break;
	}

	// Run periodic TCP tasks
	tcp_tick(ns);
}
