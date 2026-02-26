// =============================================================================
// AlJefra OS — DNS Resolver (UDP, A records only)
// =============================================================================

#include "netstack.h"

// DNS header
typedef struct {
	u16 id;
	u16 flags;
	u16 qdcount;
	u16 ancount;
	u16 nscount;
	u16 arcount;
} __attribute__((packed)) dns_header_t;

// DNS response state
static volatile u32 dns_result_ip;
static volatile u8 dns_response_received;
static u16 dns_query_id;

// Encode a hostname into DNS wire format (e.g., "api.anthropic.com" -> "\3api\9anthropic\3com\0")
static u32 dns_encode_name(u8 *buf, const char *name) {
	u8 *out = buf;
	const char *p = name;

	while (*p) {
		// Find next dot or end
		const char *dot = p;
		while (*dot && *dot != '.') dot++;
		u32 label_len = (u32)(dot - p);
		*out++ = (u8)label_len;
		for (u32 i = 0; i < label_len; i++)
			*out++ = (u8)p[i];
		p = *dot ? dot + 1 : dot;
	}
	*out++ = 0;	// Root label
	return (u32)(out - buf);
}

// Skip a DNS name (handles compression pointers)
static u8 *dns_skip_name(u8 *ptr, u8 *end) {
	while (ptr < end) {
		if (*ptr == 0) return ptr + 1;
		if ((*ptr & 0xC0) == 0xC0) return ptr + 2;	// Compression pointer
		ptr += *ptr + 1;
	}
	return end;
}

// Called by udp.c when a DNS response (from port 53) arrives
void dns_handle_response(net_state_t *ns, u8 *data, u32 len) {
	if (len < sizeof(dns_header_t))
		return;

	dns_header_t *hdr = (dns_header_t *)data;

	// Check it's a response matching our query
	if (ntohs(hdr->id) != dns_query_id)
		return;
	if (!(ntohs(hdr->flags) & 0x8000))	// QR bit must be set (response)
		return;

	u16 ancount = ntohs(hdr->ancount);
	if (ancount == 0)
		return;

	// Skip question section
	u8 *ptr = data + sizeof(dns_header_t);
	u8 *end = data + len;
	u16 qdcount = ntohs(hdr->qdcount);
	for (u16 i = 0; i < qdcount && ptr < end; i++) {
		ptr = dns_skip_name(ptr, end);
		ptr += 4;	// QTYPE + QCLASS
	}

	// Parse answers — look for A record (type 1, class 1)
	for (u16 i = 0; i < ancount && ptr < end; i++) {
		ptr = dns_skip_name(ptr, end);
		if (ptr + 10 > end) break;

		u16 rtype = ((u16)ptr[0] << 8) | ptr[1];
		u16 rclass = ((u16)ptr[2] << 8) | ptr[3];
		u16 rdlen = ((u16)ptr[8] << 8) | ptr[9];
		ptr += 10;

		if (rtype == 1 && rclass == 1 && rdlen == 4 && ptr + 4 <= end) {
			// A record — extract IPv4 address
			dns_result_ip = ((u32)ptr[0] << 24) | ((u32)ptr[1] << 16) |
			                ((u32)ptr[2] << 8) | (u32)ptr[3];
			dns_response_received = 1;
			return;
		}
		ptr += rdlen;
	}
}

// Resolve hostname to IPv4 address
// Returns IP in host byte order, or 0 on failure
u32 dns_resolve(const char *hostname) {
	net_state_t *ns = net_get_state();

	// Check DNS cache first
	for (int i = 0; i < DNS_CACHE_SIZE; i++) {
		if (ns->dns_cache[i].valid && net_strcmp(ns->dns_cache[i].name, hostname) == 0)
			return ns->dns_cache[i].ip;
	}

	// Build DNS query
	u8 buf[512];
	dns_header_t *hdr = (dns_header_t *)buf;

	// Use TSC for query ID
	dns_query_id = (u16)(b_system(TSC, 0, 0) & 0xFFFF);
	dns_response_received = 0;
	dns_result_ip = 0;

	hdr->id = htons(dns_query_id);
	hdr->flags = htons(0x0100);	// Standard query, recursion desired
	hdr->qdcount = htons(1);
	hdr->ancount = 0;
	hdr->nscount = 0;
	hdr->arcount = 0;

	// Encode question
	u32 offset = sizeof(dns_header_t);
	offset += dns_encode_name(buf + offset, hostname);

	// QTYPE = A (1)
	buf[offset++] = 0;
	buf[offset++] = 1;
	// QCLASS = IN (1)
	buf[offset++] = 0;
	buf[offset++] = 1;

	// Send query via UDP to DNS server
	udp_send(ns, ns->dns_server, 53, 53, buf, offset);

	// Wait for response (up to 3 seconds, with one retry)
	u64 start = net_get_time_ms();
	int retried = 0;
	while (!dns_response_received) {
		net_poll();
		u64 elapsed = net_get_time_ms() - start;
		if (elapsed > 3000) break;
		if (elapsed > 1500 && !retried) {
			udp_send(ns, ns->dns_server, 53, 53, buf, offset);
			retried = 1;
		}
	}

	if (dns_response_received && dns_result_ip != 0) {
		// Cache the result
		for (int i = 0; i < DNS_CACHE_SIZE; i++) {
			if (!ns->dns_cache[i].valid) {
				net_strcpy(ns->dns_cache[i].name, hostname);
				ns->dns_cache[i].ip = dns_result_ip;
				ns->dns_cache[i].valid = 1;
				break;
			}
		}
		return dns_result_ip;
	}

	return 0;
}
