// =============================================================================
// AlJefra OS — IP/TCP/UDP Checksums
// =============================================================================

#include "netstack.h"

// Generic Internet checksum (RFC 1071)
u16 ip_checksum(void *data, u32 len) {
	u32 sum = 0;
	u16 *p = (u16 *)data;

	while (len > 1) {
		sum += *p++;
		len -= 2;
	}
	// Odd byte
	if (len == 1)
		sum += *(u8 *)p;

	// Fold 32-bit sum into 16 bits
	while (sum >> 16)
		sum = (sum & 0xFFFF) + (sum >> 16);

	return (u16)~sum;
}

// TCP/UDP pseudo-header checksum
static u32 pseudo_header_sum(u32 src_ip, u32 dst_ip, u8 proto, u32 len) {
	u32 sum = 0;
	// Pseudo-header fields in network byte order
	sum += (src_ip >> 16) & 0xFFFF;
	sum += src_ip & 0xFFFF;
	sum += (dst_ip >> 16) & 0xFFFF;
	sum += dst_ip & 0xFFFF;
	sum += htons(proto);
	sum += htons((u16)len);
	return sum;
}

u16 tcp_checksum(u32 src_ip, u32 dst_ip, void *tcp_data, u32 tcp_len) {
	u32 sum = pseudo_header_sum(src_ip, dst_ip, IP_PROTO_TCP, tcp_len);
	u16 *p = (u16 *)tcp_data;
	u32 len = tcp_len;

	while (len > 1) {
		sum += *p++;
		len -= 2;
	}
	if (len == 1)
		sum += *(u8 *)p;

	while (sum >> 16)
		sum = (sum & 0xFFFF) + (sum >> 16);

	return (u16)~sum;
}

u16 udp_checksum(u32 src_ip, u32 dst_ip, void *udp_data, u32 udp_len) {
	u32 sum = pseudo_header_sum(src_ip, dst_ip, IP_PROTO_UDP, udp_len);
	u16 *p = (u16 *)udp_data;
	u32 len = udp_len;

	while (len > 1) {
		sum += *p++;
		len -= 2;
	}
	if (len == 1)
		sum += *(u8 *)p;

	while (sum >> 16)
		sum = (sum & 0xFFFF) + (sum >> 16);

	u16 result = (u16)~sum;
	// UDP: 0x0000 checksum means "no checksum", use 0xFFFF instead
	if (result == 0) result = 0xFFFF;
	return result;
}
